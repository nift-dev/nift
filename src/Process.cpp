#include "Proc.h"
#include <cstdlib>
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
bool nift_find_executable(const std::string& name,std::string& path){fs::path p(name);if(p.has_parent_path()){if(fs::exists(p)){path=fs::absolute(p).string();return true;}return false;}const char* pe=std::getenv("PATH");if(!pe)return false;
#ifdef _WIN32
char sep=';'; const char* exts[]={"",".exe",".cmd",".bat"};
#else
char sep=':'; const char* exts[]={""};
#endif
std::stringstream ss(pe);std::string d;while(std::getline(ss,d,sep))for(auto e:exts){fs::path c=fs::path(d)/(name+e);std::error_code ec;if(fs::exists(c,ec)&&!ec){
#ifndef _WIN32
if(::access(c.c_str(),X_OK)!=0)continue;
#endif
path=c.string();return true;}}return false;}
#ifndef _WIN32
static int open_temp(std::string& path){char t[]="/tmp/nift-proc-XXXXXX";int fd=mkstemp(t);if(fd>=0)path=t;return fd;}
ProcessResult nift_run_pipeline(const std::vector<ProcessSpec>& specs,bool capture,bool stream){
    ProcessResult r;if(specs.empty()){r.error="empty pipeline";return r;}std::string op,ep;int ofd=-1,efd=-1;if(capture){ofd=open_temp(op);efd=open_temp(ep);if(ofd<0||efd<0){r.error="cannot create capture files";return r;}}
    // Interactive foreground execution: the child inherits the terminal
    // directly (no capture) and is placed in its own foreground process group
    // so terminal-aware programs and Ctrl-C behave like Bash. Only simple
    // single-command foreground invocations from the shell use this path.
    const bool foreground = specs.size()==1 && specs[0].foreground_terminal;
    struct sigaction ig{}, otto{}, otin{}, oint{}, oquit{};
    if(foreground && isatty(STDIN_FILENO)){ ig.sa_handler=SIG_IGN;sigaction(SIGTTOU,&ig,&otto);sigaction(SIGTTIN,&ig,&otin);sigaction(SIGINT,&ig,&oint);sigaction(SIGQUIT,&ig,&oquit); }
    std::vector<pid_t> pids;int prev=-1;
    for(size_t i=0;i<specs.size();++i){int pp[2]={-1,-1};if(i+1<specs.size()&&pipe(pp)!=0){r.error="pipe failed";break;}pid_t pid=fork();if(pid<0){r.error="fork failed";break;}if(pid==0){
            if(specs[i].foreground_terminal)setpgid(0,0);
            if(!specs[i].cwd.empty()&&chdir(specs[i].cwd.c_str())!=0)_exit(126);for(const auto&kv:specs[i].env)setenv(kv.first.c_str(),kv.second.c_str(),1);
            if(prev>=0)dup2(prev,STDIN_FILENO);else if(!specs[i].stdin_path.empty()){int fd=open(specs[i].stdin_path.c_str(),O_RDONLY);if(fd<0)_exit(126);dup2(fd,STDIN_FILENO);close(fd);}
            if(i+1<specs.size())dup2(pp[1],STDOUT_FILENO);else if(!specs[i].stdout_path.empty()){int fl=O_WRONLY|O_CREAT|(specs[i].append_stdout?O_APPEND:O_TRUNC);int fd=open(specs[i].stdout_path.c_str(),fl,0666);if(fd<0)_exit(126);dup2(fd,STDOUT_FILENO);close(fd);}else if(capture)dup2(ofd,STDOUT_FILENO);
            if(specs[i].merge_stderr)dup2(STDOUT_FILENO,STDERR_FILENO);else if(!specs[i].stderr_path.empty()){int fl=O_WRONLY|O_CREAT|(specs[i].append_stderr?O_APPEND:O_TRUNC);int fd=open(specs[i].stderr_path.c_str(),fl,0666);if(fd<0)_exit(126);dup2(fd,STDERR_FILENO);close(fd);}else if(capture)dup2(efd,STDERR_FILENO);
            if(prev>=0)close(prev);if(pp[0]>=0)close(pp[0]);if(pp[1]>=0)close(pp[1]);if(ofd>=0)close(ofd);if(efd>=0)close(efd);
            if(specs[i].foreground_terminal){ struct sigaction z{};z.sa_handler=SIG_DFL;sigaction(SIGTTOU,&z,nullptr);sigaction(SIGTTIN,&z,nullptr);sigaction(SIGINT,&z,nullptr);sigaction(SIGQUIT,&z,nullptr); }
            std::vector<std::string> avs;avs.push_back(specs[i].program);avs.insert(avs.end(),specs[i].args.begin(),specs[i].args.end());std::vector<char*> av;for(auto&x:avs)av.push_back(x.data());av.push_back(nullptr);execvp(specs[i].program.c_str(),av.data());_exit(errno==ENOENT?127:126);
        }
        if(specs[i].foreground_terminal&&isatty(STDIN_FILENO)){ setpgid(pid,pid); tcsetpgrp(STDIN_FILENO,pid); }
        if(prev>=0)close(prev);if(pp[1]>=0)close(pp[1]);prev=pp[0];pids.push_back(pid);r.launched=true;
    }
    if(prev>=0)close(prev);int status=0;for(pid_t p:pids){int st=0;waitpid(p,&st,0);status=st;}if(r.launched)r.exit_code=WIFEXITED(status)?WEXITSTATUS(status):(WIFSIGNALED(status)?128+WTERMSIG(status):1);if(ofd>=0)close(ofd);if(efd>=0)close(efd);if(capture){r.out=read_all(op);r.err=read_all(ep);fs::remove(op);fs::remove(ep);if(stream){std::cout<<r.out;std::cerr<<r.err;}}
    if(foreground&&isatty(STDIN_FILENO)){ tcsetpgrp(STDIN_FILENO,getpgrp()); sigaction(SIGTTOU,&otto,nullptr);sigaction(SIGTTIN,&otin,nullptr);sigaction(SIGINT,&oint,nullptr);sigaction(SIGQUIT,&oquit,nullptr); }
    return r;
}
ProcessResult nift_run_process(const ProcessSpec&s,bool capture,bool stream){return nift_run_pipeline({s},capture,stream);}
#else
// Windows process backend (CreateProcessW). Semantics mirror the Unix path:
// direct spawn (no shell), real OS pipes between pipeline stages, capture via
// temporary files, per-stage cwd/env overrides, stdin/stdout/stderr routing,
// merge_stderr, append flags and last-stage exit status. NOTE: this backend is
// code-reviewed on Linux but has NOT been compiled/run here; it must be
// verified in CI on a Windows runner before release.
#include <windows.h>
#include <wchar.h>
#include <vector>
#include <string>
#include <mutex>

namespace {
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
std::string win_temp_file(std::wstring& out) {
    wchar_t dir[MAX_PATH]; if (!GetTempPathW(MAX_PATH, dir)) return "";
    wchar_t name[MAX_PATH];
    if (!GetTempFileNameW(dir, L"nift", 0, name)) return "";
    out = name;
    // GetTempFileName creates the file; truncate it for capture use.
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(n - 1, 0); WideCharToMultiByte(CP_UTF8, 0, name, -1, &utf8[0], n, nullptr, nullptr);
    return utf8;
}
struct EnvRestore {
    std::vector<std::pair<std::string, std::string>> prev;
    ~EnvRestore() { for (const auto& kv : prev) if (kv.second.empty()) SetEnvironmentVariableA(kv.first.c_str(), nullptr); else SetEnvironmentVariableA(kv.first.c_str(), kv.second.c_str()); }
};
// Set per-stage env overrides; previous values restored on destruction.
void apply_env(const std::map<std::string, std::string>& env, EnvRestore& restore) {
    for (const auto& kv : env) {
        char buf[32768]; DWORD n = GetEnvironmentVariableA(kv.first.c_str(), buf, sizeof(buf));
        restore.prev.emplace_back(kv.first, (n > 0 && n < sizeof(buf)) ? std::string(buf) : std::string{});
        SetEnvironmentVariableA(kv.first.c_str(), kv.second.c_str());
    }
}
} // namespace

ProcessResult nift_run_pipeline(const std::vector<ProcessSpec>& specs, bool capture, bool stream) {
    ProcessResult r;
    if (specs.empty()) { r.error = "empty pipeline"; return r; }
    std::wstring ow, ew; std::string op, ep;
    if (capture) { op = win_temp_file(ow); ep = win_temp_file(ew); if (op.empty() || ep.empty()) { r.error = "cannot create capture files"; return r; } }
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
            HANDLE wp; if (!CreatePipe(&next_read, &wp, &sa, 0)) { ok = false; break; }
            out = wp;
        } else if (!spec.stdout_path.empty()) out = open_redirect(spec.stdout_path, false, spec.append_stdout);
        else if (capture) out = CreateFileW(ow.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (spec.merge_stderr) err = out;
        else if (!spec.stderr_path.empty()) err = open_redirect(spec.stderr_path, false, spec.append_stderr);
        else if (capture) err = CreateFileW(ew.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = (in != INVALID_HANDLE_VALUE) ? in : GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = (out != INVALID_HANDLE_VALUE) ? out : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = (err != INVALID_HANDLE_VALUE) ? err : GetStdHandle(STD_ERROR_HANDLE);

        std::string program = spec.program;
        std::vector<std::string> args = spec.args;
        std::string resolved; bool is_script = false;
        if (nift_find_executable(spec.program, resolved)) {
            std::string low = resolved; for (auto& c : low) c = (char)tolower((unsigned char)c);
            if (low.size() > 4 && (low.substr(low.size()-4) == ".cmd" || low.substr(low.size()-4) == ".bat")) is_script = true;
        }
        std::wstring cmdline;
        if (is_script) { cmdline = L"cmd.exe /c " + widen(build_command_line(resolved, args)); program = "cmd.exe"; }
        else { if (!resolved.empty()) program = resolved; cmdline = widen(build_command_line(program, args)); }

        EnvRestore restore;
        apply_env(spec.env, restore);
        PROCESS_INFORMATION pi{};
        std::wstring cwd = spec.cwd.empty() ? L"" : widen(spec.cwd.string());
        BOOL created = CreateProcessW(nullptr, &cmdline[0], nullptr, nullptr, TRUE,
                                      0, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
        if (!created) { r.error = "CreateProcessW failed for '" + spec.program + "' (error " + std::to_string(GetLastError()) + ")"; ok = false; }
        // Parent must close its own copy of every inherited handle once the
        // child that consumes it has started; otherwise downstream readers
        // never see EOF (pipeline deadlock) and handles leak.
        if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (err != INVALID_HANDLE_VALUE && err != out) CloseHandle(err);
        prev_read = next_read;
        if (!created) break;
        CloseHandle(pi.hThread);
        procs.push_back(pi.hProcess);
        r.launched = true;
    }
    if (prev_read != INVALID_HANDLE_VALUE) CloseHandle(prev_read);
    DWORD exit = 0;
    for (std::size_t i = 0; i < procs.size(); ++i) {
        WaitForSingleObject(procs[i], INFINITE);
        GetExitCodeProcess(procs[i], &exit);
        CloseHandle(procs[i]);
    }
    if (r.launched) r.exit_code = (int)exit;
    if (capture) {
        r.out = read_all(op); r.err = read_all(ep);
        DeleteFileW(ow.c_str()); DeleteFileW(ew.c_str());
        if (stream) { std::cout << r.out; std::cerr << r.err; }
    }
    if (!ok && r.error.empty()) r.error = "pipeline failed";
    return r;
}
ProcessResult nift_run_process(const ProcessSpec& s, bool capture, bool stream) { return nift_run_pipeline({s}, capture, stream); }
#endif
