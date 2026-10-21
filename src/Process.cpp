#include "Proc.h"
#include "ReadLimit.h"
#include "ProcessTestHooks.h"
#include "PreparedProcessPOSIX.h"
#include "ProcessPOSIX.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <iostream>
#ifndef _WIN32
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <poll.h>
#include <sys/syscall.h>
extern char **environ;
#else
#include <windows.h>
#endif
namespace fs=std::filesystem;
bool nift_find_executable(const std::string& name,std::string& path){
#ifdef _WIN32
    fs::path p=fs::u8path(name);
    // Match Windows command lookup semantics closely enough for direct
    // CreateProcessW execution: for an extensionless command, prefer PATHEXT
    // candidates (.COM/.EXE/.BAT/.CMD by default) before an extensionless file.
    // An MSYS2 directory can legitimately contain both `tool` (a shebang
    // script) and `tool.cmd`; selecting the former merely because it exists
    // makes CreateProcessW fail with ERROR_BAD_EXE_FORMAT (193).
    auto win_candidates=[&](const fs::path& base){
        std::vector<fs::path> out;
        if(base.has_extension()){out.push_back(base);return out;}
        std::vector<std::string> exts;
        const char* px=std::getenv("PATHEXT");
        std::string raw=(px&&*px)?px:".COM;.EXE;.BAT;.CMD";
        std::stringstream es(raw);std::string ext;
        while(std::getline(es,ext,';')){
            if(ext.empty())continue;
            if(ext.front()!='.')ext.insert(ext.begin(),'.');
            exts.push_back(ext);
        }
        // De-duplicate PATHEXT entries case-insensitively while preserving its
        // order. The extensionless path is a final fallback, never preferred.
        std::vector<std::string> seen;
        for(const auto& e:exts){
            std::string low=e;for(char& c:low)c=(char)std::tolower((unsigned char)c);
            if(std::find(seen.begin(),seen.end(),low)!=seen.end())continue;
            seen.push_back(low);out.push_back(fs::u8path(base.u8string()+e));
        }
        out.push_back(base);
        return out;
    };
    auto accept=[&](const fs::path& c){std::error_code ec;if(fs::is_regular_file(c,ec)&&!ec){path=fs::absolute(c).u8string();return true;}return false;};
    if(p.has_parent_path()){for(const auto& c:win_candidates(p))if(accept(c))return true;return false;}
    const char* pe=std::getenv("PATH");if(!pe)return false;
    std::stringstream ss(pe);std::string d;
    while(std::getline(ss,d,';'))for(const auto& c:win_candidates(fs::u8path(d)/fs::u8path(name)))if(accept(c))return true;
    return false;
#else
    fs::path p(name);
    if(p.has_parent_path()){std::error_code ec;if(fs::is_regular_file(p,ec)&&!ec&&::access(p.c_str(),X_OK)==0){path=fs::absolute(p).string();return true;}return false;}
    const char* pe=std::getenv("PATH");if(!pe)return false;
    std::stringstream ss(pe);std::string d;
    while(std::getline(ss,d,':')){fs::path c=fs::path(d)/name;std::error_code ec;if(fs::is_regular_file(c,ec)&&!ec&&::access(c.c_str(),X_OK)==0){path=c.string();return true;}}
    return false;
#endif
}

#include "HostTrace.h"
// Runtime 2.0 CP08: one "spawn" trace line per pipeline stage.
static void nift_trace_spawn(const std::vector<ProcessSpec>& specs){
    if(!nift::trace::enabled())return;
    for(const auto& spec:specs){std::string arg=spec.program;for(const auto& a:spec.args){arg.push_back(' ');arg+=a;}nift::trace::emit("spawn",arg,spec.cwd.empty()?std::string():spec.cwd.generic_string());}
}
#ifndef _WIN32
// Runtime 2.0 CP07: capture files are anonymous regular files in /tmp (the
// same directory and file type as before), so a crash or kill can never leave
// one behind and no name is ever visible. Linux uses O_TMPFILE (never linked);
// elsewhere, or if the filesystem lacks O_TMPFILE, mkstemp is unlinked at
// once. The child still sees a seekable regular file on stdout/stderr, and
// later writes by background descendants go to the unlinked file and are
// dropped, exactly as before. `path` only labels the capture for test hooks.
static int open_temp(std::string& path, size_t stage){
    if(nift_process_test_fail("temp",stage))return -1;
    int fd=-1;
#if defined(__linux__) && defined(O_TMPFILE)
    fd=open("/tmp",O_TMPFILE|O_RDWR|O_CLOEXEC,0600);
    if(fd>=0)path="/tmp/<O_TMPFILE capture>";
#endif
    if(fd<0){
        char t[]="/tmp/nift-proc-XXXXXX";fd=mkstemp(t);
        if(fd<0)return -1;
        unlink(t);path=t;
    }
    fd=nift_process_owned_fd(fd);
    if(fd>=0)nift_process_test_temp(path);
    return fd;
}
// feature-host-deps: SIGKILL a stage and, on Linux, every descendant it
// started (found through /proc/<pid>/task/<tid>/children, each stopped first
// so it cannot fork while the tree is walked). Elsewhere only the stage.
static void kill_stage_tree(pid_t root){
#if defined(__linux__)
    std::vector<pid_t> tree{root};
    for(std::size_t i=0;i<tree.size()&&tree.size()<4096;++i){
        kill(tree[i],SIGSTOP);
        const std::string task_dir="/proc/"+std::to_string(tree[i])+"/task";
        if(DIR* tasks=opendir(task_dir.c_str())){
            while(dirent* task=readdir(tasks)){
                if(task->d_name[0]=='.')continue;
                std::ifstream children(task_dir+"/"+task->d_name+"/children");
                long child=0;
                while(children>>child)if(child>0&&std::find(tree.begin(),tree.end(),static_cast<pid_t>(child))==tree.end())tree.push_back(static_cast<pid_t>(child));
            }
            closedir(tasks);
        }
    }
    for(auto it=tree.rbegin();it!=tree.rend();++it)kill(*it,SIGKILL);
#else
    kill(root,SIGKILL);
#endif
}
// Reads the whole capture through its descriptor, from offset 0. With a
// limit (feature-host-deps, D-028) the buffer never holds or reserves more
// than `limit` bytes: one byte past it marks the capture as exceeded and
// releases it (background descendants may still be appending, so the file
// size is only a hint).
static std::string read_fd_all(int fd,std::uint64_t limit=0,bool* exceeded=nullptr){
    std::string data;char buffer[65536];off_t offset=0;
    if(limit){struct stat info{};if(fstat(fd,&info)==0&&info.st_size>0){if(static_cast<std::uint64_t>(info.st_size)>limit){if(exceeded)*exceeded=true;return {};}data.reserve(static_cast<std::size_t>(info.st_size));}}
    for(;;){
        std::size_t request=sizeof buffer;
        if(limit){const std::uint64_t held=data.size();const std::uint64_t room=held<limit?limit-held:0;if(room<request)request=static_cast<std::size_t>(room+1);}
        ssize_t n=pread(fd,buffer,request,offset);if(n<0){if(errno==EINTR)continue;break;}if(n==0)break;
        if(limit&&static_cast<std::uint64_t>(n)>limit-data.size()){std::string().swap(data);if(exceeded)*exceeded=true;return {};}
        if(limit)nift::readlimit::reserve_within(data,data.size()+static_cast<std::size_t>(n),limit);
        data.append(buffer,static_cast<std::size_t>(n));offset+=n;}
    return data;
}
// feature-site-dx: parent-side wording for a failure the child reported.
static std::string exec_failure_message(const ProcessSpec& spec,const PreparedProcessPOSIX& prepared,int kind,int error,int detail){
    switch(kind){
    case PreparedProcessPOSIX::ExecNotFound: return "executable not found: "+spec.program;
    case PreparedProcessPOSIX::ExecDenied: return "permission denied: "+spec.program;
    case PreparedProcessPOSIX::ExecInterpreter: {
        std::string interpreter;
        if(detail>=0&&static_cast<std::size_t>(detail)<prepared.candidates.size()){
            std::ifstream in(prepared.candidates[static_cast<std::size_t>(detail)],std::ios::binary);std::string line;
            if(std::getline(in,line)&&line.rfind("#!",0)==0){std::size_t b=line.find_first_not_of(" \t",2);if(b!=std::string::npos){std::size_t e=line.find_first_of(" \t\r",b);interpreter=line.substr(b,e==std::string::npos?std::string::npos:e-b);}}
        }
        if(!interpreter.empty())return "interpreter not found: "+interpreter+" (from "+spec.program+")";
        return "cannot execute "+spec.program+": its interpreter or loader was not found";
    }
    case PreparedProcessPOSIX::ExecSetup:
        switch(detail){
        case PreparedProcessPOSIX::SetupChdir: return "cannot change directory to "+spec.cwd.generic_string()+": "+std::strerror(error);
        case PreparedProcessPOSIX::SetupStdin: return "cannot open stdin "+spec.stdin_path+": "+std::strerror(error);
        case PreparedProcessPOSIX::SetupStdout: return "cannot open stdout "+spec.stdout_path+": "+std::strerror(error);
        case PreparedProcessPOSIX::SetupStderr: return "cannot open stderr "+spec.stderr_path+": "+std::strerror(error);
        default: return "redirection setup failed for "+spec.program+": "+std::strerror(error);
        }
    default: return "cannot execute "+spec.program+": "+std::strerror(error);
    }
}
ProcessResult nift_run_pipeline(const std::vector<ProcessSpec>& specs,bool capture,bool stream,std::uint64_t capture_limit){
    nift_trace_spawn(specs);
    std::unique_lock<std::mutex> launch_lock(nift_process_launch_mutex());
    ProcessResult r;if(specs.empty()){r.error="empty pipeline";return r;}std::string op,ep;int ofd=-1,efd=-1;if(capture){ofd=open_temp(op,0);efd=open_temp(ep,1);if(ofd<0||efd<0){if(ofd>=0)close(ofd);if(efd>=0)close(efd);r.error="cannot create capture files";return r;}}
    // Interactive foreground execution: the child inherits the terminal
    // directly (no capture) and is placed in its own foreground process group
    // so terminal-aware programs and Ctrl-C behave like Bash. Only simple
    // single-command foreground invocations from the shell use this path.
    const bool foreground = specs.size()==1 && specs[0].foreground_terminal;
    struct sigaction ig{}, otto{}, otin{}, oint{}, oquit{};
    if(foreground && isatty(STDIN_FILENO)){ ig.sa_handler=SIG_IGN;sigaction(SIGTTOU,&ig,&otto);sigaction(SIGTTIN,&ig,&otin);sigaction(SIGINT,&ig,&oint);sigaction(SIGQUIT,&ig,&oquit); }
    std::vector<PreparedProcessPOSIX> prepared;prepared.reserve(specs.size());
    for(const auto& spec:specs)prepared.emplace_back(spec);
    std::vector<pid_t> pids;pids.reserve(specs.size());int prev=-1;
    // feature-site-dx: the first stage whose exec failed (reported by the
    // child over a close-on-exec pipe). r.launched means every stage exec'd.
    std::string exec_error;
    std::vector<std::string> stage_errors(specs.size());
    for(size_t i=0;i<specs.size();++i){int pp[2]={-1,-1};if(i+1<specs.size()&&(nift_process_test_fail("pipe",i)||!nift_process_pipe(pp))){r.error="pipe failed";break;}int report[2]={-1,-1};if(!nift_process_pipe(report))report[0]=report[1]=-1;const bool fail_in=nift_process_test_fail("dup2-in",i), fail_out=nift_process_test_fail("dup2-out",i), fail_err=nift_process_test_fail("dup2-err",i);pid_t pid=nift_process_test_fail("fork",i)?-1:fork();if(pid<0){if(pp[0]>=0)close(pp[0]);if(pp[1]>=0)close(pp[1]);if(report[0]>=0)close(report[0]);if(report[1]>=0)close(report[1]);r.error="fork failed";break;}if(pid==0){
            if (report[0] >= 0) close(report[0]);
            if(specs[i].foreground_terminal)setpgid(0,0);
            if (!specs[i].cwd.empty() && chdir(specs[i].cwd.c_str()) != 0) PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupChdir);
            if(prev>=0){if(!nift_process_dup2(prev,STDIN_FILENO,fail_in))PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupRedirect);}else if(!specs[i].stdin_path.empty()){int fd=open(specs[i].stdin_path.c_str(),O_RDONLY);if(fd<0)PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupStdin);if(!nift_process_dup2(fd,STDIN_FILENO,fail_in))PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupRedirect);if(fd!=STDIN_FILENO)close(fd);}
            if(i+1<specs.size()){if(!nift_process_dup2(pp[1],STDOUT_FILENO,fail_out))PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupRedirect);}else if(!specs[i].stdout_path.empty()){int fl=O_WRONLY|O_CREAT|(specs[i].append_stdout?O_APPEND:O_TRUNC);int fd=open(specs[i].stdout_path.c_str(),fl,0666);if(fd<0)PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupStdout);if(!nift_process_dup2(fd,STDOUT_FILENO,fail_out))PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupRedirect);if(fd!=STDOUT_FILENO)close(fd);}else if(capture){if(!nift_process_dup2(ofd,STDOUT_FILENO,fail_out))PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupRedirect);}
            if(specs[i].merge_stderr){if(!nift_process_dup2(STDOUT_FILENO,STDERR_FILENO,fail_err))PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupRedirect);}else if(!specs[i].stderr_path.empty()){int fl=O_WRONLY|O_CREAT|(specs[i].append_stderr?O_APPEND:O_TRUNC);int fd=open(specs[i].stderr_path.c_str(),fl,0666);if(fd<0)PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupStderr);if(!nift_process_dup2(fd,STDERR_FILENO,fail_err))PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupRedirect);if(fd!=STDERR_FILENO)close(fd);}else if(capture){if(!nift_process_dup2(efd,STDERR_FILENO,fail_err))PreparedProcessPOSIX::fail_exec(report[1],PreparedProcessPOSIX::ExecSetup,errno,126,PreparedProcessPOSIX::SetupRedirect);}
            if (prev >= 0) close(prev);
            if (pp[0] >= 0) close(pp[0]);
            if (pp[1] >= 0) close(pp[1]);
            if (ofd >= 0) close(ofd);
            if (efd >= 0) close(efd);
            if(specs[i].foreground_terminal){ struct sigaction z{};z.sa_handler=SIG_DFL;sigaction(SIGTTOU,&z,nullptr);sigaction(SIGTTIN,&z,nullptr);sigaction(SIGINT,&z,nullptr);sigaction(SIGQUIT,&z,nullptr); }
            prepared[i].exec(report[1]);
        }
        if (report[1] >= 0) close(report[1]);
        if (report[0] >= 0) {
            int payload[3]={0,0,0};std::size_t got=0;
            while(got<sizeof payload){ssize_t n=read(report[0],reinterpret_cast<char*>(payload)+got,sizeof payload-got);if(n<0&&errno==EINTR)continue;if(n<=0)break;got+=static_cast<std::size_t>(n);}
            close(report[0]);
            if(got==sizeof payload){
                const std::string message=exec_failure_message(specs[i],prepared[i],payload[0],payload[1],payload[2]);
                stage_errors[i]=message;
                if(exec_error.empty())exec_error=message;
            }
        }
        if(specs[i].foreground_terminal&&isatty(STDIN_FILENO)){ setpgid(pid,pid); tcsetpgrp(STDIN_FILENO,pid); }
        if (prev >= 0) close(prev);
        if (pp[1] >= 0) close(pp[1]);
        prev = pp[0];
        nift_process_test_child(pid);
        pids.push_back(pid);
        r.launched = true;
    }
    if (prev >= 0) close(prev);
    // A peer may be waiting forever for a stage that was never launched.
    // SIGKILL bounds teardown even for stopped children or ignored SIGTERM.
    if(!r.error.empty())for(pid_t p:pids)kill(p,SIGKILL);
    launch_lock.unlock();
    int status = 0;
    auto status_code = [](int st) { return WIFEXITED(st) ? WEXITSTATUS(st) : (WIFSIGNALED(st) ? 128 + WTERMSIG(st) : 1); };
    r.stages.assign(specs.size(), ProcessStageResult{});
    // feature-host-deps (D-028): with a capture limit the captures are watched
    // while the stages run. Once either holds more than the limit, every stage
    // still running is killed (SIGKILL, like the setup-failure path above) and
    // reaped, so a never-ending producer cannot fill the disk or hang Nift.
    // The check runs between short waits (pidfd poll on Linux, a capped sleep
    // elsewhere), so the files may overshoot by what the stages write in one
    // interval (at most 2 ms). On Linux the stages' descendants are killed too
    // (kill_stage_tree); elsewhere only the stages (no per-pipeline process
    // group before CP16, D-030 C3).
    std::vector<int> stage_status(pids.size(), 0);
    if (capture && capture_limit && !pids.empty()) {
        std::vector<bool> reaped(pids.size(), false);
        std::size_t running = pids.size();
        bool killed = false;
        auto over_limit = [&]() -> const char* {
            struct stat info{};
            if (ofd >= 0 && fstat(ofd, &info) == 0 && static_cast<std::uint64_t>(info.st_size) > capture_limit) return "stdout";
            if (efd >= 0 && fstat(efd, &info) == 0 && static_cast<std::uint64_t>(info.st_size) > capture_limit) return "stderr";
            return nullptr;
        };
        long pause_us = 50;
        while (running) {
            for (std::size_t index = 0; index < pids.size(); ++index) {
                if (reaped[index]) continue;
                int st = 0;
                const pid_t done = waitpid(pids[index], &st, killed ? 0 : WNOHANG);
                if (done == pids[index] || (done < 0 && errno != EINTR)) { reaped[index] = true; stage_status[index] = st; --running; }
            }
            if (!running || killed) continue;
            if (const char* which = over_limit()) {
                r.capture_limit_exceeded = which;
                for (std::size_t index = 0; index < pids.size(); ++index) if (!reaped[index]) kill_stage_tree(pids[index]);
                killed = true;
                continue;
            }
            bool waited = false;
#if defined(__linux__) && defined(SYS_pidfd_open)
            for (std::size_t index = 0; index < pids.size() && !waited; ++index) {
                if (reaped[index]) continue;
                const int pidfd = static_cast<int>(syscall(SYS_pidfd_open, pids[index], 0));
                if (pidfd >= 0) { struct pollfd pfd{pidfd, POLLIN, 0}; poll(&pfd, 1, 2); close(pidfd); waited = true; }
            }
#endif
            if (!waited) { usleep(static_cast<useconds_t>(pause_us)); if (pause_us < 2000) pause_us *= 2; }
        }
    } else {
        for (std::size_t index = 0; index < pids.size(); ++index)
            while (waitpid(pids[index], &stage_status[index], 0) < 0 && errno == EINTR) {}
    }
    for (std::size_t index = 0; index < pids.size(); ++index) {
        const int st = stage_status[index];
        status = st;
        auto& stage = r.stages[index];
        stage.exit_code = status_code(st);
        stage.launched = stage_errors[index].empty() && r.error.empty();
        stage.error = stage_errors[index];
    }
    for (std::size_t index = pids.size(); index < specs.size(); ++index) r.stages[index].error = r.error.empty() ? "not started" : r.error;
    if (r.launched && r.error.empty()) r.exit_code = status_code(status);
    // An exec failure keeps its shell-convention exit status (127/126) but is
    // not a launch (matches Windows, where CreateProcessW fails up front).
    // Round 2 (N2): for a pipeline (more than one stage) the top-level
    // fields keep their pre-feature meaning (launched = the stages were
    // started; error only for setup failures in the parent); per-stage
    // launch failures are reported in r.stages.
    if (specs.size() == 1 && r.launched && r.error.empty() && !exec_error.empty()) { r.launched = false; r.error = exec_error; r.exec_failed = true; }
    if (capture) {
        bool out_exceeded=!r.capture_limit_exceeded.empty(),err_exceeded=false;
        if(!out_exceeded) r.out = read_fd_all(ofd,capture_limit,&out_exceeded);
        if(!out_exceeded) r.err = read_fd_all(efd,capture_limit,&err_exceeded);
        if(out_exceeded||err_exceeded){r.out.clear();r.err.clear();if(r.capture_limit_exceeded.empty())r.capture_limit_exceeded=out_exceeded?"stdout":"stderr";stream=false;}
        if (stream) { std::cout << r.out; std::cerr << r.err; }
    }
    if (ofd >= 0) close(ofd);
    if (efd >= 0) close(efd);
    if(foreground&&isatty(STDIN_FILENO)){ tcsetpgrp(STDIN_FILENO,getpgrp()); sigaction(SIGTTOU,&otto,nullptr);sigaction(SIGTTIN,&otin,nullptr);sigaction(SIGINT,&oint,nullptr);sigaction(SIGQUIT,&oquit,nullptr); }
    return r;
}
ProcessResult nift_run_process(const ProcessSpec&s,bool capture,bool stream,std::uint64_t capture_limit){return nift_run_pipeline({s},capture,stream,capture_limit);}
#else
// Windows process backend (CreateProcessW). Semantics mirror the Unix path:
// direct spawn (no shell), real OS pipes between pipeline stages, capture via
// temporary files, per-stage cwd/env overrides, stdin/stdout/stderr routing,
// merge_stderr, append flags and last-stage exit status. NOTE: this backend is
// covered by the native Windows process contract and failure matrix.
#include <windows.h>
#include <wchar.h>
#include "ChildProcessHook.h"
#include <vector>
#include <string>
#include <mutex>

namespace {
std::string narrow(const std::wstring& value) {
    if(value.empty())return {};
    int size=WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);
    if(size<=0)return {};
    std::string result(size,0);WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),result.data(),size,nullptr,nullptr);return result;
}
std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}
// Quote one argv element using Windows command-line rules: wrap in quotes when
// it contains spaces/tabs/quotes; embedded quotes become backslash-escaped.
std::string quote_win_arg(const std::string& a) {
    if (a.empty()) return "\"\"";
    if (a.find_first_of(" \t\"") == std::string::npos) return a;
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (char c : a) {
        if (c == '\\') { ++backslashes; continue; }
        if (c == '"') { out.append(backslashes * 2 + 1, '\\'); out += '"'; backslashes = 0; continue; }
        out.append(backslashes, '\\'); backslashes = 0; out += c;
    }
    out.append(backslashes * 2, '\\');
    out += '"';
    return out;
}
std::string build_command_line(const std::string& program, const std::vector<std::string>& args) {
    std::string cmd = quote_win_arg(program);
    for (const auto& a : args) { cmd += ' '; cmd += quote_win_arg(a); }
    return cmd;
}
// Create an inheritable kernel handle for redirection, or INVALID_HANDLE_VALUE.
HANDLE open_redirect(const std::string& path, bool read, bool append) {
    if (path.empty()) return INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = nullptr;
    // Append is implemented by seeking to end after open; GENERIC_WRITE opens
    // with the same semantics (FILE_APPEND_DATA is an access right, not a file
    // attribute, and its value 0x4 collides with FILE_ATTRIBUTE_SYSTEM).
    DWORD access = read ? GENERIC_READ : GENERIC_WRITE;
    DWORD share = read ? FILE_SHARE_READ : 0;
    DWORD disp = read ? OPEN_EXISTING : (append ? OPEN_ALWAYS : CREATE_ALWAYS);
    HANDLE h = CreateFileW(widen(path).c_str(), access, share, &sa, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    if (!read && append) { SetFilePointer(h, 0, nullptr, FILE_END); }
    return h;
}
// Runtime 2.0 CP07: besides the named file (needed so each stage can open its
// own inheritable write handle), a non-inheritable "keeper" handle opened with
// FILE_FLAG_DELETE_ON_CLOSE makes Windows delete the file once the last handle
// closes, even if Nift crashes or a background descendant still holds a
// write handle (previously DeleteFileW failed then and the file was leaked).
// Every other handle to the file must therefore share delete access. The
// capture is read back through the keeper handle.
std::string win_temp_file(std::wstring& out, size_t stage, HANDLE& keeper) {
    keeper = INVALID_HANDLE_VALUE;
    if(nift_process_test_fail("temp",stage))return "";
    wchar_t dir[MAX_PATH]; DWORD length=GetTempPathW(MAX_PATH,dir);if(!length || length>=MAX_PATH)return "";
    wchar_t name[MAX_PATH];
    if (!GetTempFileNameW(dir, L"nift", 0, name)) return "";
    out = name;
    // GetTempFileName creates the file; truncate it for capture use.
    HANDLE h = nift_process_test_fail("temp-open",stage)?INVALID_HANDLE_VALUE:CreateFileW(name, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(h==INVALID_HANDLE_VALUE){DeleteFileW(name);out.clear();return "";}
    CloseHandle(h);
    keeper = CreateFileW(name, GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if(keeper==INVALID_HANDLE_VALUE){DeleteFileW(name);out.clear();return "";}
    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
    if(n<=0){CloseHandle(keeper);keeper=INVALID_HANDLE_VALUE;out.clear();return "";}
    std::string utf8(n, 0); WideCharToMultiByte(CP_UTF8, 0, name, -1, &utf8[0], n, nullptr, nullptr);utf8.pop_back();
    nift_process_test_temp(utf8);
    return utf8;
}
// Windows environment names are case insensitive. Retain special =C: drive
// entries from the inherited wide block and overlay without touching the parent.
struct EnvLess {
    bool operator()(const std::wstring& a,const std::wstring& b) const {
        return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_LESS_THAN;
    }
};
std::vector<wchar_t> child_environment(const std::map<std::string,std::string>& overrides) {
    std::map<std::wstring,std::wstring,EnvLess> values;
    wchar_t* block=GetEnvironmentStringsW();
    if(!block)return {};
    for(const wchar_t* p=block;*p;p+=wcslen(p)+1){
        std::wstring entry=p; auto eq=entry.find(L'=',entry[0]==L'='?1:0);
        if(eq!=std::wstring::npos)values[entry.substr(0,eq)]=entry.substr(eq+1);
    }
    FreeEnvironmentStringsW(block);
    for(const auto& kv:overrides)values[widen(kv.first)]=widen(kv.second);
    std::vector<wchar_t> out;
    for(const auto& kv:values){std::wstring entry=kv.first+L"="+kv.second;out.insert(out.end(),entry.begin(),entry.end());out.push_back(0);}
    if(out.empty())out.push_back(0);
    out.push_back(0);return out;
}
// Duplicating inherited stdio avoids changing the parent's handle flags.
HANDLE child_stdio(HANDLE source,DWORD access,size_t stage) {
    HANDLE out=INVALID_HANDLE_VALUE;
    if(nift_process_test_fail("duplicate",stage))return out;
    if(source && source!=INVALID_HANDLE_VALUE){
        if(DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&out,0,TRUE,DUPLICATE_SAME_ACCESS))return out;
        return INVALID_HANDLE_VALUE;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    return CreateFileW(L"NUL",access,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
}
} // namespace

ProcessResult nift_run_pipeline(const std::vector<ProcessSpec>& specs, bool capture, bool stream, std::uint64_t capture_limit) {
    nift_trace_spawn(specs);
    ProcessResult r;
    int exec_failure_code = 0;
    if (specs.empty()) { r.error = "empty pipeline"; return r; }
    std::wstring ow, ew; std::string op, ep; HANDLE okeep = INVALID_HANDLE_VALUE, ekeep = INVALID_HANDLE_VALUE;
    auto close_keepers = [&]() { if (okeep != INVALID_HANDLE_VALUE) CloseHandle(okeep); if (ekeep != INVALID_HANDLE_VALUE) CloseHandle(ekeep); okeep = ekeep = INVALID_HANDLE_VALUE; };
    if (capture) { op = win_temp_file(ow,0,okeep); ep = win_temp_file(ew,1,ekeep); if (op.empty() || ep.empty()) { close_keepers(); if(!ow.empty())DeleteFileW(ow.c_str());if(!ew.empty())DeleteFileW(ew.c_str());r.error = "cannot create capture files"; return r; } }
    // All child stderr duplicates share one write-capable file object and
    // position. Append-only handles are rejected by MSYS stdio adapters;
    // reopening per stage would instead truncate or overwrite earlier bytes.
    HANDLE capture_error=INVALID_HANDLE_VALUE;
    if(capture){
        capture_error=nift_process_test_fail("capture-file",1)?INVALID_HANDLE_VALUE:CreateFileW(ew.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(capture_error==INVALID_HANDLE_VALUE){close_keepers();DeleteFileW(ow.c_str());DeleteFileW(ew.c_str());r.error="cannot open stderr capture file";return r;}
    }
    std::vector<HANDLE> procs;
    HANDLE prev_read = INVALID_HANDLE_VALUE;
    bool ok = true;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const ProcessSpec& spec = specs[i];
        // stdin: inherited pipe read end, explicit file, or the parent stdin.
        HANDLE in = INVALID_HANDLE_VALUE;
        if (prev_read != INVALID_HANDLE_VALUE) in = prev_read;
        else if (!spec.stdin_path.empty()) in = open_redirect(spec.stdin_path, true, false);
        // stdout: next-stage pipe write end, explicit file, or capture file.
        HANDLE next_read = INVALID_HANDLE_VALUE;
        HANDLE out = INVALID_HANDLE_VALUE, err = INVALID_HANDLE_VALUE;
        if (i + 1 < specs.size()) {
            SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = nullptr;
            HANDLE wp; if (nift_process_test_fail("pipe",i)||!CreatePipe(&next_read, &wp, &sa, 0)) { if(in!=INVALID_HANDLE_VALUE && in!=prev_read)CloseHandle(in);r.error="pipe failed";ok = false; break; }
            out = wp;
        } else if (!spec.stdout_path.empty()) out = open_redirect(spec.stdout_path, false, spec.append_stdout);
        else if (capture) { SECURITY_ATTRIBUTES ca{}; ca.nLength = sizeof(ca); ca.bInheritHandle = TRUE; out = nift_process_test_fail("capture-out",i)?INVALID_HANDLE_VALUE:CreateFileW(ow.c_str(), GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, &ca, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); }
        if (spec.merge_stderr) err = out;
        else if (!spec.stderr_path.empty()) err = open_redirect(spec.stderr_path, false, spec.append_stderr);
        else if(capture)err=nift_process_test_fail("capture-err",i)?INVALID_HANDLE_VALUE:capture_error;

        // Requested routing must fail closed: INVALID is not inherited stdio.
        bool routing_ok=(prev_read!=INVALID_HANDLE_VALUE || spec.stdin_path.empty() || in!=INVALID_HANDLE_VALUE)
            && (i+1<specs.size() || (spec.stdout_path.empty()&&!capture) || out!=INVALID_HANDLE_VALUE)
            && (spec.merge_stderr || (spec.stderr_path.empty()&&!capture) || err!=INVALID_HANDLE_VALUE);
        HANDLE inherited[3]={INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE};
        if(routing_ok){
            inherited[0]=child_stdio(in==INVALID_HANDLE_VALUE?GetStdHandle(STD_INPUT_HANDLE):in,GENERIC_READ,i*3);
            inherited[1]=child_stdio(out==INVALID_HANDLE_VALUE?GetStdHandle(STD_OUTPUT_HANDLE):out,GENERIC_WRITE,i*3+1);
            inherited[2]=child_stdio(spec.merge_stderr?inherited[1]:(err==INVALID_HANDLE_VALUE?GetStdHandle(STD_ERROR_HANDLE):err),GENERIC_WRITE,i*3+2);
            for(HANDLE h:inherited)if(h==INVALID_HANDLE_VALUE)routing_ok=false;
        }
        STARTUPINFOEXW sx{};sx.StartupInfo.cb=sizeof(sx);
        sx.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        sx.StartupInfo.hStdInput=inherited[0];sx.StartupInfo.hStdOutput=inherited[1];sx.StartupInfo.hStdError=inherited[2];
        SIZE_T attribute_size=0;
        InitializeProcThreadAttributeList(nullptr,1,0,&attribute_size);
        std::vector<unsigned char> attribute_storage(attribute_size);
        sx.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
        bool attributes_initialized=false;
        if(routing_ok){
            attributes_initialized=!nift_process_test_fail("attributes-init",i) && InitializeProcThreadAttributeList(sx.lpAttributeList,1,0,&attribute_size)!=FALSE;
            routing_ok=attributes_initialized && !nift_process_test_fail("attributes-update",i) && UpdateProcThreadAttribute(sx.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)!=FALSE;
        }

        std::string program = spec.program;
        std::vector<std::string> args = spec.args;
        std::string resolved; bool is_script = false; bool nift_script = false;
        if (nift_find_executable(spec.program, resolved)) {
            std::string low = resolved; for (auto& c : low) c = (char)tolower((unsigned char)c);
            if (low.size() > 4 && (low.substr(low.size()-4) == ".cmd" || low.substr(low.size()-4) == ".bat")) is_script = true;
            // Nift executable .f scripts have no OS-level shebang execution on
            // Windows, so running ./script.f directly (from the Nift shell, command
            // style, or run()) must route through `nift <script> <args>`.
            // Spawn the current executable so the same binary interprets it.
            if (low.size() > 2 && low.substr(low.size()-2) == ".f") nift_script = true;
        }
        std::wstring cmdline;
        if (is_script) { cmdline = L"cmd.exe /c " + widen(build_command_line(resolved, args)); program = "cmd.exe"; }
        else if (nift_script) {
            wchar_t self[MAX_PATH];
            const DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
            program = (n > 0 && n < MAX_PATH) ? narrow(std::wstring(self,n)) : "nift";
            std::vector<std::string> runner; runner.reserve(1 + args.size());
            runner.push_back(resolved);
            runner.insert(runner.end(), args.begin(), args.end());
            cmdline = widen(build_command_line(program, runner));
        }
        else { if (!resolved.empty()) program = resolved; cmdline = widen(build_command_line(program, args)); }

        DWORD setup_error=routing_ok?ERROR_SUCCESS:GetLastError();
        auto environment=child_environment(spec.env);
        PROCESS_INFORMATION pi{};
        std::wstring cwd = spec.cwd.empty() ? L"" : spec.cwd.wstring();
        (void)nift_process_test_fail("launch",i);
        BOOL created=FALSE;
        if(routing_ok && !environment.empty() && !nift_process_test_fail("spawn",i))
            created=CreateProcessW(nullptr,cmdline.data(),nullptr,nullptr,TRUE,
                EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT,environment.data(),
                cwd.empty()?nullptr:cwd.c_str(),&sx.StartupInfo,&pi);
        DWORD launch_error=routing_ok?GetLastError():setup_error;
        if(attributes_initialized)DeleteProcThreadAttributeList(sx.lpAttributeList);
        for(HANDLE h:inherited)if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
        if (!created) {
            // feature-site-dx: same wording and exit status as POSIX for the
            // two common cases (launched stays false; 127/126 follow the
            // shell convention). Other failures keep the detailed message.
            // Single commands only (round 2, N2): a pipeline keeps its
            // pre-feature top-level result; r.stages carries the stage error.
            if (specs.size() == 1 && routing_ok && (launch_error == ERROR_FILE_NOT_FOUND || launch_error == ERROR_PATH_NOT_FOUND)) { r.error = "executable not found: " + spec.program; exec_failure_code = 127; }
            else if (specs.size() == 1 && routing_ok && launch_error == ERROR_ACCESS_DENIED) { r.error = "permission denied: " + spec.program; exec_failure_code = 126; }
            else r.error = (routing_ok?"CreateProcessW failed for '":"redirection/stdio setup failed for '") + spec.program + "' at stage " + std::to_string(i) + " (error " + std::to_string(launch_error) + ")";
            ok = false;
        }
        // Parent must close its own copy of every inherited handle once the
        // child that consumes it has started; otherwise downstream readers
        // never see EOF (pipeline deadlock) and handles leak.
        if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (err != INVALID_HANDLE_VALUE && err != out && err != capture_error) CloseHandle(err);
        prev_read = next_read;
        if (!created) break;
        CloseHandle(pi.hThread);
        nift_process_test_child(pi.dwProcessId);
        if (nift::child_process_spawned) {  // an owning build records it (ProjectOwnership.cpp)
            FILETIME created_at{}, exited_at{}, kernel{}, user{};
            if (GetProcessTimes(pi.hProcess, &created_at, &exited_at, &kernel, &user))
                nift::child_process_spawned(pi.dwProcessId,
                    (static_cast<unsigned long long>(created_at.dwHighDateTime) << 32) | created_at.dwLowDateTime);
        }
        procs.push_back(pi.hProcess);
        r.launched = true;
    }
    if (prev_read != INVALID_HANDLE_VALUE) CloseHandle(prev_read);
    if(capture_error!=INVALID_HANDLE_VALUE)CloseHandle(capture_error);
    if(!ok)for(HANDLE h:procs)TerminateProcess(h,126);
    DWORD exit = 0;
    // feature-site-dx (review R6): per-stage results, as on POSIX.
    const std::string stage_failure = r.error;
    r.stages.assign(specs.size(), ProcessStageResult{});
    for (std::size_t i = 0; i < procs.size(); ++i) {
        DWORD waited=WaitForSingleObject(procs[i],ok?INFINITE:5000);
        if(waited!=WAIT_OBJECT_0){r.error += " process cleanup/wait failed";ok=false;}
        GetExitCodeProcess(procs[i], &exit);
        CloseHandle(procs[i]);
        r.stages[i].launched = true;
        r.stages[i].exit_code = (int)exit;
    }
    for (std::size_t i = procs.size(); i < specs.size(); ++i) {
        r.stages[i].error = i == procs.size() && !stage_failure.empty() ? stage_failure : "not started";
        if (i == procs.size() && exec_failure_code) r.stages[i].exit_code = exec_failure_code;
    }
    if (r.launched && ok) r.exit_code = (int)exit;
    // feature-site-dx: launched means every stage started (as on POSIX).
    if (exec_failure_code) { r.launched = false; r.exit_code = exec_failure_code; }
    if (capture) {
        // Read through the keeper handles: the named file may not be opened
        // again without FILE_SHARE_DELETE, and closing the keepers deletes it.
        // feature-host-deps (D-028): bounded like the POSIX read_fd_all.
        auto read_handle = [capture_limit](HANDLE h, bool& exceeded) {
            std::string data; LARGE_INTEGER zero{}; if (!SetFilePointerEx(h, zero, nullptr, FILE_BEGIN)) return data;
            char buffer[65536]; DWORD got = 0;
            for (;;) {
                DWORD request = sizeof buffer;
                if (capture_limit) { const std::uint64_t room = data.size() < capture_limit ? capture_limit - data.size() : 0; if (room < request) request = static_cast<DWORD>(room + 1); }
                if (!ReadFile(h, buffer, request, &got, nullptr) || got == 0) break;
                if (capture_limit && static_cast<std::uint64_t>(got) > capture_limit - data.size()) { std::string().swap(data); exceeded = true; return data; }
                if (capture_limit) nift::readlimit::reserve_within(data, data.size() + got, capture_limit);
                data.append(buffer, got);
            }
            return data;
        };
        bool out_exceeded = false, err_exceeded = false;
        r.out = read_handle(okeep, out_exceeded);
        if (!out_exceeded) r.err = read_handle(ekeep, err_exceeded);
        if (out_exceeded || err_exceeded) { r.out.clear(); r.err.clear(); r.capture_limit_exceeded = out_exceeded ? "stdout" : "stderr"; stream = false; }
        // MSYS2/Cygwin children write CRLF to redirected output in text mode,
        // so normalize captured output to LF; exact-output callers (run(),
        // cmd().run(), tests comparing stdout) otherwise see trailing \r.
        auto normalize = [](std::string& s) {
            std::string n; n.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
                n.push_back(s[i]);
            }
            s = std::move(n);
        };
        normalize(r.out); normalize(r.err);
        close_keepers();
        // Harmless if the keepers already deleted the files; kept for the
        // case where a keeper could not take delete ownership.
        DeleteFileW(ow.c_str()); DeleteFileW(ew.c_str());
        if (stream) { std::cout << r.out; std::cerr << r.err; }
    }
    if (!ok && r.error.empty()) r.error = "pipeline failed";
    return r;
}
ProcessResult nift_run_process(const ProcessSpec& s, bool capture, bool stream, std::uint64_t capture_limit) { return nift_run_pipeline({s}, capture, stream, capture_limit); }
#endif
