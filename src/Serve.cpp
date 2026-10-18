// `nift serve` — see Serve.h and docs/runtime-2/checkpoints/feature-serve.md.
//
// Lifecycle (owner decision):
//  - POSIX: every check runs first (directory, Python 3.8+, port bind probe),
//    then the banner is printed and Python replaces Nift via exec. There is no
//    supervisor: Ctrl+C/SIGTERM reach Python directly, nothing can be
//    orphaned, and the exit status is Python's own.
//  - Windows: launch-and-wait through the existing nift_run_process (inherited
//    console, argv vector, no capture) and return Python's exit code. When a
//    kill-on-close job object can be established, a force-killed Nift cannot
//    orphan Python; otherwise a warning is printed (see join_kill_on_close_job).
#include "Serve.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
// winsock2.h must precede <windows.h> (pulled in by Console.h below).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

#include "Proc.h"
#include "ProjectRead.h"
// Console.h pulls <windows.h> on Windows: keep it after the standard headers
// (same rule as CLI.cpp).
#include "Console.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* usage_text = "usage: nift serve [--port N] [--host H] [--dir PATH]";
constexpr int probe_timeout_seconds = 5;

struct ServeOptions {
    int port = 8000;
    std::string host = "127.0.0.1";
    std::string dir;  // empty: the project's configured output dir
    bool have_port = false, have_host = false, have_dir = false;
    bool help = false;
};

void print_help() {
    std::cout << usage_text << "\n\n"
              << "Serve a directory over HTTP for local development, using Python 3.8+:\n"
              << "  python3 -m http.server PORT --bind HOST --directory DIR\n\n"
              << "  --port N     port, 1-65535 (default 8000)\n"
              << "  --host H     address or host name to bind (default 127.0.0.1)\n"
              << "  --dir PATH   directory to serve (default: the project's output dir)\n\n"
              << "Run 'nift build' first. Not a production server.\n";
}

bool parse_port(const std::string& text, int& port) {
    if (text.empty() || text.size() > 5) return false;
    for (char c : text) if (c < '0' || c > '9') return false;
    const int value = std::stoi(text);
    if (value < 1 || value > 65535) return false;
    port = value;
    return true;
}

bool parse_options(int argc, char** argv, ServeOptions& options) {
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { options.help = true; continue; }
        std::string name = arg, value;
        bool inline_value = false;
        const auto eq = arg.find('=');
        if (arg.rfind("--", 0) == 0 && eq != std::string::npos) {
            name = arg.substr(0, eq);
            value = arg.substr(eq + 1);
            inline_value = true;
        }
        if (name != "--port" && name != "--host" && name != "--dir") {
            if (!arg.empty() && arg[0] == '-') console::error("unknown serve option '" + arg + "'");
            else console::error("serve takes no positional arguments (got '" + arg + "'); use --dir PATH");
            std::cerr << "  " << usage_text << '\n';
            return false;
        }
        if (!inline_value) {
            // `--dir --port` is a missing value, not a directory named --port;
            // a value that really starts with "--" can use --dir=--name.
            if (i + 1 >= argc || std::string(argv[i + 1]).rfind("--", 0) == 0) {
                console::error(name + " requires a value");
                return false;
            }
            value = argv[++i];
        }
        bool& seen = name == "--port" ? options.have_port : name == "--host" ? options.have_host : options.have_dir;
        if (seen) { console::error(name + " was given more than once"); return false; }
        seen = true;
        if (name == "--port") {
            if (!parse_port(value, options.port)) {
                console::error("invalid port '" + value + "'; expected an integer from 1 to 65535");
                return false;
            }
        } else if (name == "--host") {
            if (value.empty()) { console::error("--host requires a non-empty host name or address"); return false; }
            options.host = value;
        } else {
            if (value.empty()) { console::error("--dir requires a non-empty path"); return false; }
            options.dir = value;
        }
    }
    return true;
}

// Same rule as ProjectInfo's root discovery: both .nift/config.json and
// .nift/tracked.json must exist. Read-only.
fs::path find_root() {
    std::error_code ec;
    fs::path path = fs::absolute(fs::current_path(ec), ec);
    if (ec) return {};
    while (true) {
        if (fs::exists(path / ".nift/config.json", ec) && fs::exists(path / ".nift/tracked.json", ec)) return path;
        const fs::path parent = path.parent_path();
        if (parent == path) return {};
        path = parent;
    }
}

bool resolve_directory(const ServeOptions& options, fs::path& out) {
    std::error_code ec;
    fs::path dir;
    bool configured = false;
    if (!options.dir.empty()) {
        // Filesystem freedom: absolute or outside the project is fine.
        dir = fs::u8path(options.dir);
    } else {
        const fs::path root = find_root();
        if (root.empty()) {
            console::error("not a Nift project");
            std::cerr << "  run 'nift serve' inside a project, or pass --dir PATH to serve any directory\n";
            return false;
        }
        Config config;
        std::string error;
        if (!project_read::load_config(root, config, error)) {
            console::error(".nift/config.json: " + error);
            return false;
        }
        dir = root / fs::u8path(config.output_dir);
        configured = true;
    }
    // No lexical normalisation: `link/../www` must mean what the kernel says
    // (the parent of link's target), so resolution is left to the filesystem
    // and the canonical path is what is served and shown.
    dir = fs::absolute(dir, ec);
    if (ec) { console::error("cannot resolve directory '" + options.dir + "': " + ec.message()); return false; }
    if (!fs::exists(dir, ec)) {
        if (configured) {
            console::error("output directory does not exist: " + dir.u8string());
            std::cerr << "  run 'nift build' first, or pass --dir PATH\n";
        } else {
            console::error("directory does not exist: " + dir.u8string());
        }
        return false;
    }
    if (!fs::is_directory(dir, ec)) {
        console::error("not a directory: " + dir.u8string());
        return false;
    }
    const fs::path canonical = fs::canonical(dir, ec);
    if (ec) { console::error("cannot resolve directory " + dir.u8string() + ": " + ec.message()); return false; }
    out = canonical;
    return true;
}

struct Python {
    std::string program;                // absolute path of the interpreter/launcher
    std::vector<std::string> prefix;    // e.g. {"-3"} for the Windows py launcher
};

struct ProbeResult {
    bool launched = false, timed_out = false;
    int status = -1;
    std::string err;  // probe stderr (POSIX only), shown on failure
};

#ifndef _WIN32
// Version probe bounded to probe_timeout_seconds. A small local fork/exec
// rather than nift_run_process, which has no timeout: stdin/stdout to
// /dev/null, stderr through a pipe (shown on failure, e.g. pyenv shim
// messages). On Linux the probe dies with Nift (PR_SET_PDEATHSIG); elsewhere
// a Nift killed by SIGKILL during the probe leaves it until it exits.
ProbeResult run_probe(const std::string& program, const std::vector<std::string>& args) {
    ProbeResult r;
    std::vector<std::string> storage{program};
    storage.insert(storage.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : storage) argv.push_back(a.data());
    argv.push_back(nullptr);
    int ends[2];
    if (pipe(ends) != 0) return r;
    fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    fcntl(ends[1], F_SETFD, FD_CLOEXEC);
#ifdef __linux__
    const pid_t parent = getpid();
#endif
    const pid_t pid = fork();
    if (pid < 0) { close(ends[0]); close(ends[1]); return r; }
    if (pid == 0) {
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent) _exit(127);
#endif
        const int null_fd = open("/dev/null", O_RDWR);
        if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0 || dup2(null_fd, STDOUT_FILENO) < 0 ||
            dup2(ends[1], STDERR_FILENO) < 0)
            _exit(127);
        execv(program.c_str(), argv.data());
        _exit(127);
    }
    close(ends[1]);
    r.launched = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(probe_timeout_seconds);
    int status = 0;
    bool reaped = false, eof = false;
    while (!reaped) {
        if (waitpid(pid, &status, WNOHANG) == pid) { reaped = true; }
        if (!eof) {
            // Drain stderr; a short poll keeps the exit check responsive even
            // if a grandchild keeps the pipe open.
            pollfd pfd{ends[0], POLLIN, 0};
            if (poll(&pfd, 1, reaped ? 0 : 50) > 0) {
                char buffer[1024];
                const ssize_t n = read(ends[0], buffer, sizeof buffer);
                if (n > 0) { if (r.err.size() < 4096) r.err.append(buffer, static_cast<std::size_t>(n)); }
                else if (n == 0) eof = true;
            }
        } else if (!reaped) {
            usleep(20000);
        }
        if (!reaped && std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            r.timed_out = true;
            reaped = true;
        }
    }
    close(ends[0]);
    if (!r.timed_out) r.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    if (r.status == 127) r.launched = false;
    return r;
}
#else
// Windows: the existing launcher, output discarded. No timeout here; the
// kill-on-close job (joined before probing) ends a probe if Nift is killed.
ProbeResult run_probe(const std::string& program, const std::vector<std::string>& args) {
    ProcessSpec spec;
    spec.program = program;
    spec.args = args;
    spec.stdout_path = "NUL";
    spec.stderr_path = "NUL";
    const ProcessResult result = nift_run_process(spec, /*capture=*/false);
    ProbeResult r;
    r.launched = result.launched;
    r.status = result.exit_code;
    return r;
}

bool has_exe_extension(const std::string& path) {
    if (path.size() < 4) return false;
    std::string ext = path.substr(path.size() - 4);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".exe";
}
#endif

#ifndef _WIN32
// nift_find_executable (shared with the shell and which()) skips files that
// are not executable, so a miss can hide a `python3` that exists but lacks
// the execute bit. Say so instead of "not found".
std::string non_executable_on_path(const std::string& name) {
    const char* raw = std::getenv("PATH");
    if (!raw) return {};
    const std::string path_list = raw;
    std::size_t start = 0;
    while (start <= path_list.size()) {
        const std::size_t end = path_list.find(':', start);
        const std::string dir = path_list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const fs::path candidate = fs::path(dir.empty() ? "." : dir) / name;
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec) && access(candidate.c_str(), X_OK) != 0) return candidate.string();
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}
#endif

std::string indent_lines(const std::string& text) {
    std::string out, line;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) out += "      " + line + '\n';
            line.clear();
        } else {
            line.push_back(text[i]);
        }
    }
    return out;
}

bool find_python(Python& python) {
    struct Candidate { const char* name; std::vector<std::string> prefix; };
#ifdef _WIN32
    // The py launcher first; then python3/python, accepting only real .exe
    // files (a .bat/.cmd shim would route the argv through cmd.exe, where &,
    // | and % in paths are interpreted).
    const std::vector<Candidate> candidates = {{"py", {"-3"}}, {"python3", {}}, {"python", {}}};
#else
    const std::vector<Candidate> candidates = {{"python3", {}}, {"python", {}}};
#endif
    // 3.8+: http.server's address-family selection (IPv6 --bind) arrived in
    // 3.8, and 3.7 is end-of-life. The probe is valid Python 2 syntax, so an
    // old `python` reports cleanly. -E -s -B: ignore PYTHON* variables and
    // user site-packages, write no bytecode.
    const std::vector<std::string> probe = {"-E", "-s", "-B", "-c",
                                            "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 3)"};
    std::vector<std::string> tried;
    bool skipped_shim = false;
    for (const auto& candidate : candidates) {
        std::string path;
        const std::string label = candidate.prefix.empty() ? candidate.name : std::string(candidate.name) + " -3";
        if (!nift_find_executable(candidate.name, path)) {
#ifndef _WIN32
            const std::string blocked = non_executable_on_path(candidate.name);
            if (!blocked.empty()) { tried.push_back(label + " (" + blocked + "): not executable"); continue; }
#endif
            tried.push_back(label + ": not found");
            continue;
        }
#ifdef _WIN32
        if (!has_exe_extension(path)) {
            tried.push_back(label + " (" + path + "): skipped, not an .exe (a .bat/.cmd shim would run through cmd.exe)");
            skipped_shim = true;
            continue;
        }
#endif
        std::vector<std::string> args = candidate.prefix;
        args.insert(args.end(), probe.begin(), probe.end());
        const ProbeResult result = run_probe(path, args);
        if (result.launched && !result.timed_out && result.status == 0) {
            python.program = path;
            python.prefix = candidate.prefix;
            return true;
        }
        std::string reason =
            result.timed_out ? "did not respond to a version check within " + std::to_string(probe_timeout_seconds) + "s"
            : !result.launched ? "could not be run"
            : result.status == 3 ? "not Python 3.8 or newer"
            : "version check failed with status " + std::to_string(result.status);
        tried.push_back(label + " (" + path + "): " + reason + '\n' + indent_lines(result.err));
    }
    console::error("'nift serve' requires Python 3.8 or newer, which was not found on PATH");
    for (const auto& line : tried) {
        std::cerr << "  tried " << line;
        if (line.empty() || line.back() != '\n') std::cerr << '\n';
    }
    if (skipped_shim) std::cerr << "  put a python.exe on PATH, or install the py launcher\n";
    std::cerr << "  install Python 3 (Nift never installs it), or serve the directory with any static file server\n";
    return false;
}

// Best-effort bind probe so an occupied port usually gives a Nift error before
// Python starts. It is racy on every platform: a port free here can be taken
// before Python binds it, and Python's own bind error (stderr, nonzero exit)
// is then what the user sees. Mirrors http.server: the first
// getaddrinfo(AI_PASSIVE) result and dual-stack IPv6. POSIX uses SO_REUSEADDR
// (as Python does). Windows uses SO_EXCLUSIVEADDRUSE, because Python's
// SO_REUSEADDR there would let it share a port another server holds.
#ifndef _WIN32
using socket_t = int;
constexpr socket_t bad_socket = -1;
int last_socket_error() { return errno; }
void close_socket(socket_t s) { close(s); }
std::string socket_error_text(int error) { return std::strerror(error); }
constexpr int err_in_use = EADDRINUSE, err_access = EACCES, err_not_avail = EADDRNOTAVAIL;
#else
using socket_t = SOCKET;
constexpr socket_t bad_socket = INVALID_SOCKET;
int last_socket_error() { return WSAGetLastError(); }
void close_socket(socket_t s) { closesocket(s); }
std::string socket_error_text(int error) { return "Winsock error " + std::to_string(error); }
constexpr int err_in_use = WSAEADDRINUSE, err_access = WSAEACCES, err_not_avail = WSAEADDRNOTAVAIL;
#endif

bool probe_port(const std::string& host, int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return true;  // best effort: Python reports bind errors
    struct Cleanup { ~Cleanup() { WSACleanup(); } } cleanup;
#endif
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* found = nullptr;
    const std::string service = std::to_string(port);
    const int status = getaddrinfo(host.c_str(), service.c_str(), &hints, &found);
    if (status != 0 || !found) {
        console::error("cannot resolve host '" + host + "': " + gai_strerror(status));
        return false;
    }
    const socket_t fd = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (fd == bad_socket) {
        console::error("cannot create a socket for host '" + host + "': " + socket_error_text(last_socket_error()));
        freeaddrinfo(found);
        return false;
    }
    int on = 1, off = 0;
#ifndef _WIN32
    // Never inherited: close-on-exec as well as closed before exec.
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#else
    // Closed before launch, and nift_run_process passes an explicit handle list.
    setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof on);
#endif
    if (found->ai_family == AF_INET6)
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof off);
    const int bound = bind(fd, found->ai_addr, static_cast<int>(found->ai_addrlen));
    const int error = last_socket_error();
    close_socket(fd);
    freeaddrinfo(found);
    if (bound == 0) return true;
    if (error == err_in_use) {
        console::error("port " + service + " is already in use on " + host);
        std::cerr << "  stop the other server or choose another port with --port N\n";
    } else if (error == err_access) {
        console::error("cannot bind port " + service + " on " + host + ": permission denied or port reserved");
        std::cerr << "  ports below 1024 usually need privileges; try --port 8000\n";
    } else if (error == err_not_avail) {
        console::error("cannot bind to '" + host + "': address not available on this machine");
    } else {
        console::error("cannot bind " + host + ":" + service + ": " + socket_error_text(error));
    }
    return false;
}

std::string url_host(const std::string& host) {
    return host.find(':') != std::string::npos ? "[" + host + "]" : host;
}

#ifdef _WIN32
// Ctrl+C/Ctrl+Break reach every process on the console. Python handles them;
// Nift just keeps waiting so it can return Python's status. A handler (unlike
// SetConsoleCtrlHandler(NULL, TRUE)) is not inherited, so Python still
// receives the interrupt.
BOOL WINAPI serve_ctrl_handler(DWORD type) {
    return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT;
}

// Force-kill protection: Nift joins a kill-on-close job and its children
// (version probes, Python) inherit the membership. The handle is never
// closed, so it closes when Nift's process ends, however it ends, and Windows
// kills whatever is left in the job. This holds ONLY when the job was
// established. If it cannot be created, configured or assigned (e.g. an
// enclosing job forbids nesting before Windows 8), serve still runs, prints a
// warning, and a force-killed Nift (taskkill /F) then leaves Python running;
// Ctrl+C and closing the console still stop it.
void join_kill_on_close_job() {
    std::string reason;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        reason = "CreateJobObject failed, error " + std::to_string(GetLastError());
    } else {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits))
            reason = "SetInformationJobObject failed, error " + std::to_string(GetLastError());
        else if (!AssignProcessToJobObject(job, GetCurrentProcess()))
            reason = "AssignProcessToJobObject failed, error " + std::to_string(GetLastError());
        if (!reason.empty()) CloseHandle(job);
    }
    if (!reason.empty())
        std::cerr << "nift serve: warning: could not arrange for the server to stop if nift is force-killed ("
                  << reason << "); stop it with Ctrl+C\n";
}
#endif

}  // namespace

int run_serve(int argc, char** argv) {
    ServeOptions options;
    if (!parse_options(argc, argv, options)) return 1;
    if (options.help) { print_help(); return 0; }

    fs::path dir;
    if (!resolve_directory(options, dir)) return 1;

#ifdef _WIN32
    join_kill_on_close_job();
#endif
    Python python;
    if (!find_python(python)) return 1;

    if (!probe_port(options.host, options.port)) return 1;

    // Argument vector only, never a shell string.
    //  -I  isolated: the cwd/project is not on sys.path (a socketserver.py or
    //      mimetypes.py there cannot shadow the standard library), PYTHON*
    //      variables and user site-packages are ignored;
    //  -B  write no __pycache__ anywhere;
    //  -u  unbuffered, so the request log is visible through pipes.
    // `--opt=value` keeps a host or path beginning with '-' from being read as
    // an option by argparse.
    std::vector<std::string> args = python.prefix;
    args.insert(args.end(), {"-I", "-B", "-u", "-m", "http.server", std::to_string(options.port),
                             "--bind=" + options.host, "--directory=" + dir.u8string()});

    std::cout << console::heading("Nift development server") << "\n\n"
              << "Serving: " << dir.u8string() << '\n'
              << "URL:     http://" << url_host(options.host) << ':' << options.port << "/\n\n"
              << "Press Ctrl+C to stop.\n"
              << std::flush;

#ifndef _WIN32
    std::vector<char*> exec_argv;
    exec_argv.push_back(const_cast<char*>(python.program.c_str()));
    for (auto& arg : args) exec_argv.push_back(arg.data());
    exec_argv.push_back(nullptr);
    std::fflush(nullptr);
    execvp(python.program.c_str(), exec_argv.data());
    const int error = errno;
    console::error("cannot start " + python.program + ": " + std::strerror(error));
    return error == ENOENT ? 127 : 126;
#else
    SetConsoleCtrlHandler(serve_ctrl_handler, TRUE);
    ProcessSpec spec;
    spec.program = python.program;
    spec.args = args;
    const ProcessResult result = nift_run_process(spec, /*capture=*/false);
    SetConsoleCtrlHandler(serve_ctrl_handler, FALSE);
    if (!result.launched) {
        console::error("cannot start " + python.program + (result.error.empty() ? "" : ": " + result.error));
        return 1;
    }
    // STATUS_CONTROL_C_EXIT: Python ended by Ctrl+C/Ctrl+Break (Ctrl+Break is
    // not a KeyboardInterrupt). A normal interrupted exit, reported as 130.
    if (static_cast<unsigned long>(static_cast<unsigned int>(result.exit_code)) == 0xC000013AUL) return 130;
    if (result.exit_code != 0) {
        console::error("Python exited with status " + std::to_string(result.exit_code));
        std::cerr << "  if the port is already in use, choose another with --port N\n";
    }
    return result.exit_code;
#endif
}
