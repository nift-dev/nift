#include "Proc.h"
#include "ProcessTestHooks.h"
#include "PreparedProcessPOSIX.h"
#include "ProcessPOSIX.h"
#include <cstdlib>
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
extern char **environ;
#else
#include <windows.h>
#endif
namespace fs=std::filesystem;
static std::string read_all(const fs::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
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
#ifndef _WIN32
static int open_temp(std::string& path, size_t stage){if(nift_process_test_fail("temp",stage))return -1;char t[]="/tmp/nift-proc-XXXXXX";int fd=mkstemp(t);if(fd>=0){fd=nift_process_owned_fd(fd);if(fd<0){int error=errno;unlink(t);errno=error;return -1;}path=t;nift_process_test_temp(path);}return fd;}
ProcessResult nift_run_pipeline(const std::vector<ProcessSpec>& specs,bool capture,bool stream){
    std::unique_lock<std::mutex> launch_lock(nift_process_launch_mutex());
    ProcessResult r;if(specs.empty()){r.error="empty pipeline";return r;}std::string op,ep;int ofd=-1,efd=-1;if(capture){ofd=open_temp(op,0);efd=open_temp(ep,1);if(ofd<0||efd<0){if(ofd>=0)close(ofd);if(efd>=0)close(efd);if(!op.empty())unlink(op.c_str());if(!ep.empty())unlink(ep.c_str());r.error="cannot create capture files";return r;}}
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
    for(size_t i=0;i<specs.size();++i){int pp[2]={-1,-1};if(i+1<specs.size()&&(nift_process_test_fail("pipe",i)||!nift_process_pipe(pp))){r.error="pipe failed";break;}const bool fail_in=nift_process_test_fail("dup2-in",i), fail_out=nift_process_test_fail("dup2-out",i), fail_err=nift_process_test_fail("dup2-err",i);pid_t pid=nift_process_test_fail("fork",i)?-1:fork();if(pid<0){if(pp[0]>=0)close(pp[0]);if(pp[1]>=0)close(pp[1]);r.error="fork failed";break;}if(pid==0){
            if(specs[i].foreground_terminal)setpgid(0,0);
            if (!specs[i].cwd.empty() && chdir(specs[i].cwd.c_str()) != 0) _exit(126);
            if(prev>=0){if(!nift_process_dup2(prev,STDIN_FILENO,fail_in))_exit(126);}else if(!specs[i].stdin_path.empty()){int fd=open(specs[i].stdin_path.c_str(),O_RDONLY);if(fd<0)_exit(126);if(!nift_process_dup2(fd,STDIN_FILENO,fail_in))_exit(126);if(fd!=STDIN_FILENO)close(fd);}
            if(i+1<specs.size()){if(!nift_process_dup2(pp[1],STDOUT_FILENO,fail_out))_exit(126);}else if(!specs[i].stdout_path.empty()){int fl=O_WRONLY|O_CREAT|(specs[i].append_stdout?O_APPEND:O_TRUNC);int fd=open(specs[i].stdout_path.c_str(),fl,0666);if(fd<0)_exit(126);if(!nift_process_dup2(fd,STDOUT_FILENO,fail_out))_exit(126);if(fd!=STDOUT_FILENO)close(fd);}else if(capture){if(!nift_process_dup2(ofd,STDOUT_FILENO,fail_out))_exit(126);}
            if(specs[i].merge_stderr){if(!nift_process_dup2(STDOUT_FILENO,STDERR_FILENO,fail_err))_exit(126);}else if(!specs[i].stderr_path.empty()){int fl=O_WRONLY|O_CREAT|(specs[i].append_stderr?O_APPEND:O_TRUNC);int fd=open(specs[i].stderr_path.c_str(),fl,0666);if(fd<0)_exit(126);if(!nift_process_dup2(fd,STDERR_FILENO,fail_err))_exit(126);if(fd!=STDERR_FILENO)close(fd);}else if(capture){if(!nift_process_dup2(efd,STDERR_FILENO,fail_err))_exit(126);}
            if (prev >= 0) close(prev);
            if (pp[0] >= 0) close(pp[0]);
            if (pp[1] >= 0) close(pp[1]);
            if (ofd >= 0) close(ofd);
            if (efd >= 0) close(efd);
            if(specs[i].foreground_terminal){ struct sigaction z{};z.sa_handler=SIG_DFL;sigaction(SIGTTOU,&z,nullptr);sigaction(SIGTTIN,&z,nullptr);sigaction(SIGINT,&z,nullptr);sigaction(SIGQUIT,&z,nullptr); }
            prepared[i].exec();
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
    for (pid_t p : pids) {
        int st = 0;
        while(waitpid(p, &st, 0)<0 && errno==EINTR){};
        status = st;
    }
    if (r.launched && r.error.empty()) r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
    if (ofd >= 0) close(ofd);
    if (efd >= 0) close(efd);
    if (capture) {
        r.out = read_all(op);
        r.err = read_all(ep);
        fs::remove(op);
        fs::remove(ep);
        if (stream) { std::cout << r.out; std::cerr << r.err; }
    }
    if(foreground&&isatty(STDIN_FILENO)){ tcsetpgrp(STDIN_FILENO,getpgrp()); sigaction(SIGTTOU,&otto,nullptr);sigaction(SIGTTIN,&otin,nullptr);sigaction(SIGINT,&oint,nullptr);sigaction(SIGQUIT,&oquit,nullptr); }
    return r;
}
ProcessResult nift_run_process(const ProcessSpec&s,bool capture,bool stream){return nift_run_pipeline({s},capture,stream);}
#else
// Windows process backend (CreateProcessW). Semantics mirror the Unix path:
// direct spawn (no shell), real OS pipes between pipeline stages, capture via
// temporary files, per-stage cwd/env overrides, stdin/stdout/stderr routing,
// merge_stderr, append flags and last-stage exit status. NOTE: this backend is
// covered by the native Windows process contract and failure matrix.
#include <windows.h>
#include <wchar.h>
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
std::string win_temp_file(std::wstring& out, size_t stage) {
    if(nift_process_test_fail("temp",stage))return "";
    wchar_t dir[MAX_PATH]; DWORD length=GetTempPathW(MAX_PATH,dir);if(!length || length>=MAX_PATH)return "";
    wchar_t name[MAX_PATH];
    if (!GetTempFileNameW(dir, L"nift", 0, name)) return "";
    out = name;
    // GetTempFileName creates the file; truncate it for capture use.
    HANDLE h = nift_process_test_fail("temp-open",stage)?INVALID_HANDLE_VALUE:CreateFileW(name, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(h==INVALID_HANDLE_VALUE){DeleteFileW(name);out.clear();return "";}
    CloseHandle(h);
    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
    if(n<=0){DeleteFileW(name);out.clear();return "";}
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

ProcessResult nift_run_pipeline(const std::vector<ProcessSpec>& specs, bool capture, bool stream) {
    ProcessResult r;
    if (specs.empty()) { r.error = "empty pipeline"; return r; }
    std::wstring ow, ew; std::string op, ep;
    if (capture) { op = win_temp_file(ow,0); ep = win_temp_file(ew,1); if (op.empty() || ep.empty()) { if(!ow.empty())DeleteFileW(ow.c_str());if(!ew.empty())DeleteFileW(ew.c_str());r.error = "cannot create capture files"; return r; } }
    // All child stderr duplicates share one write-capable file object and
    // position. Append-only handles are rejected by MSYS stdio adapters;
    // reopening per stage would instead truncate or overwrite earlier bytes.
    HANDLE capture_error=INVALID_HANDLE_VALUE;
    if(capture){
        capture_error=nift_process_test_fail("capture-file",1)?INVALID_HANDLE_VALUE:CreateFileW(ew.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(capture_error==INVALID_HANDLE_VALUE){DeleteFileW(ow.c_str());DeleteFileW(ew.c_str());r.error="cannot open stderr capture file";return r;}
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
        else if (capture) { SECURITY_ATTRIBUTES ca{}; ca.nLength = sizeof(ca); ca.bInheritHandle = TRUE; out = nift_process_test_fail("capture-out",i)?INVALID_HANDLE_VALUE:CreateFileW(ow.c_str(), GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, &ca, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); }
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
        if (!created) { r.error = (routing_ok?"CreateProcessW failed for '":"redirection/stdio setup failed for '") + spec.program + "' at stage " + std::to_string(i) + " (error " + std::to_string(launch_error) + ")"; ok = false; }
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
        procs.push_back(pi.hProcess);
        r.launched = true;
    }
    if (prev_read != INVALID_HANDLE_VALUE) CloseHandle(prev_read);
    if(capture_error!=INVALID_HANDLE_VALUE)CloseHandle(capture_error);
    if(!ok)for(HANDLE h:procs)TerminateProcess(h,126);
    DWORD exit = 0;
    for (std::size_t i = 0; i < procs.size(); ++i) {
        DWORD waited=WaitForSingleObject(procs[i],ok?INFINITE:5000);
        if(waited!=WAIT_OBJECT_0){r.error += " process cleanup/wait failed";ok=false;}
        GetExitCodeProcess(procs[i], &exit);
        CloseHandle(procs[i]);
    }
    if (r.launched && ok) r.exit_code = (int)exit;
    if (capture) {
        r.out = read_all(fs::u8path(op)); r.err = read_all(fs::u8path(ep));
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
        DeleteFileW(ow.c_str()); DeleteFileW(ew.c_str());
        if (stream) { std::cout << r.out; std::cerr << r.err; }
    }
    if (!ok && r.error.empty()) r.error = "pipeline failed";
    return r;
}
ProcessResult nift_run_process(const ProcessSpec& s, bool capture, bool stream) { return nift_run_pipeline({s}, capture, stream); }
#endif
