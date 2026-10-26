#pragma once
// CP16 Host layer, POSIX internals: descriptors (close-on-exec above stdio,
// pipes, capture files, pidfds, SIGPIPE-safe writes) and exec preparation
// with legacy PreparedProcessPOSIX semantics, used between fork() and exec
// (async-signal-safe only there). Not part of the Host layer's interface.
#ifndef _WIN32
#include "rt/host/process.h"

#include <map>
#include <string>
#include <cerrno>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace nift::rt::host::posix {

int fd_above_stdio_cloexec(int fd);
bool make_pipe(int ends[2]);
void set_nonblocking(int fd);
void close_fd(int& fd);
int open_capture_file();
int open_pidfd(pid_t pid);
// Writes without letting SIGPIPE kill Nift (blocked in this thread around the write).
ssize_t write_no_sigpipe(int fd, const char* data, std::size_t size);
bool terminal_foreground();

// ---- exec preparation (legacy PreparedProcessPOSIX semantics; the Host
// layer becomes the shared implementation when legacy moves onto it, §11.11)
enum ExecFailure : int { ExecNotFound = 1, ExecDenied = 2, ExecOther = 3, ExecInterpreter = 4, ExecSetup = 5 };
enum SetupStep : int {
    SetupChdir = 1,
    SetupStdin = 2,
    SetupStdout = 3,
    SetupStderr = 4,
    SetupRedirect = 5,
    SetupGroup = 6,
    SetupTerminal = 7
};

[[noreturn]] void child_fail(int report_fd, int kind, int error, int code, int detail = 0);
bool child_dup2(int source, int target);
void child_take_terminal(int report_fd);
void child_open_onto(int report, const std::string& path, int flags, int target, int step);

struct PreparedExec {
    std::vector<std::string> arguments, environment, candidates;
    std::vector<char*> argv, envp, shell_argv;
    std::string cwd, stdin_path, stdout_path, stderr_path;
    bool stdout_append = false, stderr_append = false;

    PreparedExec(const SpawnStage& stage, const std::map<std::string, std::string>& base_env) {
        arguments = stage.argv();
        for (auto& a : arguments) argv.push_back(a.data());
        argv.push_back(nullptr);
        std::map<std::string, std::string> vars = base_env;
        for (const auto& kv : stage.env) vars[kv.first] = kv.second;
        for (const auto& kv : vars) environment.push_back(kv.first + "=" + kv.second);
        for (auto& e : environment) envp.push_back(e.data());
        envp.push_back(nullptr);
        if (stage.program.find('/') != std::string::npos) candidates.push_back(stage.program);
        else {
            auto it = vars.find("PATH");
            const std::string path = it == vars.end() ? "/bin:/usr/bin" : it->second;
            std::size_t start = 0;
            for (;;) {
                const auto end = path.find(':', start);
                const std::string dir = path.substr(start, end == std::string::npos ? end : end - start);
                candidates.push_back(dir.empty() ? stage.program : dir + "/" + stage.program);
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
        shell_argv.push_back(const_cast<char*>("/bin/sh"));
        shell_argv.push_back(nullptr);
        for (std::size_t i = 1; i < arguments.size(); ++i) shell_argv.push_back(arguments[i].data());
        shell_argv.push_back(nullptr);
        cwd = stage.cwd;
        stdin_path = stage.stdin_path;
        stdout_path = stage.stdout_path;
        stderr_path = stage.stderr_path;
        stdout_append = stage.stdout_append;
        stderr_append = stage.stderr_append;
    }

    [[noreturn]] void exec(int report_fd) {
        bool denied = false;
        int last = ENOENT;
        int interpreter = -1;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            const auto& candidate = candidates[index];
            ::execve(candidate.c_str(), argv.data(), envp.data());
            const int error = errno;
            if (error == ENOEXEC) {
                shell_argv[1] = const_cast<char*>(candidate.c_str());
                ::execve("/bin/sh", shell_argv.data(), envp.data());
                child_fail(report_fd, ExecOther, errno, 126);
            }
            if (error == EACCES) { denied = true; continue; }
            if (error == ENOENT && interpreter < 0 && ::access(candidate.c_str(), F_OK) == 0)
                interpreter = static_cast<int>(index);
            if (error != ENOENT && error != ENOTDIR) child_fail(report_fd, ExecOther, error, 126);
            last = error;
        }
        if (denied) child_fail(report_fd, ExecDenied, EACCES, 126);
        if (interpreter >= 0) child_fail(report_fd, ExecInterpreter, ENOENT, 127, interpreter);
        child_fail(report_fd, ExecNotFound, last, 127);
    }
};

std::string exec_failure_message(const SpawnStage& stage, const PreparedExec& prepared, int kind, int error,
                                 int detail);

} // namespace nift::rt::host::posix
#endif  // !_WIN32
