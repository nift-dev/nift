// `nift serve` — see Serve.h and docs/runtime-2/checkpoints/feature-serve.md.
//
// Lifecycle (owner decision):
//  - POSIX: every check runs first (directory, Python 3, port bind probe),
//    then the banner is printed and Python replaces Nift via exec. There is no
//    supervisor: Ctrl+C/SIGTERM reach Python directly, nothing can be
//    orphaned, and the exit status is Python's own.
//  - Windows: launch-and-wait through the existing nift_run_process (inherited
//    console, argv vector, no capture) and return Python's exit code.
#include "Serve.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "Proc.h"
#include "ProjectRead.h"
// Console.h pulls <windows.h> on Windows: keep it after the standard headers
// (same rule as CLI.cpp).
#include "Console.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

struct ServeOptions {
    int port = 8000;
    std::string host = "127.0.0.1";
    std::string dir;  // empty: the project's configured output dir
    bool have_port = false, have_host = false, have_dir = false;
};

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
            std::cerr << "  usage: nift serve [--port N] [--host H] [--dir PATH]\n";
            return false;
        }
        if (!inline_value) {
            if (i + 1 >= argc) { console::error(name + " requires a value"); return false; }
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
    dir = fs::absolute(dir, ec).lexically_normal();
    if (ec) { console::error("cannot resolve directory '" + options.dir + "': " + ec.message()); return false; }
    if (dir.has_relative_path() && dir.filename().empty()) dir = dir.parent_path();
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
    out = dir;
    return true;
}

struct Python {
    std::string program;                // absolute path of the interpreter/launcher
    std::vector<std::string> prefix;    // e.g. {"-3"} for the Windows py launcher
};

bool find_python(Python& python) {
    struct Candidate { const char* name; std::vector<std::string> prefix; };
    std::vector<Candidate> candidates = {{"python3", {}}, {"python", {}}};
#ifdef _WIN32
    candidates.push_back({"py", {"-3"}});
    const char* null_device = "NUL";
#else
    const char* null_device = "/dev/null";
#endif
    // --directory (used below) needs Python 3.7+. The probe is valid Python 2
    // syntax so an old `python` reports cleanly instead of crashing.
    const std::string probe = "import sys; sys.exit(0 if sys.version_info >= (3, 7) else 3)";
    std::vector<std::string> tried;
    for (const auto& candidate : candidates) {
        std::string path;
        const std::string label = candidate.prefix.empty() ? candidate.name : std::string(candidate.name) + " -3";
        if (!nift_find_executable(candidate.name, path)) { tried.push_back(label + ": not found"); continue; }
        ProcessSpec spec;
        spec.program = path;
        spec.args = candidate.prefix;
        spec.args.push_back("-c");
        spec.args.push_back(probe);
        spec.stdout_path = null_device;
        spec.stderr_path = null_device;
        const ProcessResult result = nift_run_process(spec, /*capture=*/false);
        if (result.launched && result.exit_code == 0) {
            python.program = path;
            python.prefix = candidate.prefix;
            return true;
        }
        tried.push_back(label + " (" + path + "): " +
                        (!result.launched ? "could not be run"
                         : result.exit_code == 3 ? "not Python 3.7 or newer"
                         : "probe failed with status " + std::to_string(result.exit_code)));
    }
    console::error("'nift serve' requires Python 3.7 or newer, which was not found on PATH");
    for (const auto& line : tried) std::cerr << "  tried " << line << '\n';
    std::cerr << "  install Python 3 (Nift never installs it), or serve the directory with any static file server\n";
    return false;
}

#ifndef _WIN32
// Quick bind probe so an occupied port gives a Nift error before Python
// starts. Mirrors http.server: the first getaddrinfo(AI_PASSIVE) result,
// SO_REUSEADDR, and dual-stack for IPv6. The probe socket is close-on-exec
// and closed before exec (a never-listened socket leaves no TIME_WAIT). A
// free port here is not a guarantee (another process may bind it before
// Python does); Python's own bind failure then stays visible on stderr and
// is its nonzero exit status, since Python is the process.
bool probe_port(const std::string& host, int port) {
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
    const int fd = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (fd < 0) {
        console::error("cannot create a socket for host '" + host + "': " + std::strerror(errno));
        freeaddrinfo(found);
        return false;
    }
    // Never inherited: close-on-exec as well as closed before exec.
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    int on = 1, off = 0;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (found->ai_family == AF_INET6) setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    const int bound = bind(fd, found->ai_addr, found->ai_addrlen);
    const int error = errno;
    close(fd);
    freeaddrinfo(found);
    if (bound == 0) return true;
    if (error == EADDRINUSE) {
        console::error("port " + service + " is already in use on " + host);
        std::cerr << "  stop the other server or choose another port with --port N\n";
    } else if (error == EACCES) {
        console::error("permission denied binding port " + service + " on " + host);
        std::cerr << "  ports below 1024 usually need privileges; try --port 8000\n";
    } else if (error == EADDRNOTAVAIL) {
        console::error("cannot bind to '" + host + "': address not available on this machine");
    } else {
        console::error("cannot bind " + host + ":" + service + ": " + std::strerror(error));
    }
    return false;
}
#endif

std::string url_host(const std::string& host) {
    return host.find(':') != std::string::npos ? "[" + host + "]" : host;
}

#ifdef _WIN32
// Ctrl+C/Ctrl+Break reach every process on the console. Python handles them
// (KeyboardInterrupt, exit 0); Nift just keeps waiting so it can return
// Python's status. A handler (unlike SetConsoleCtrlHandler(NULL, TRUE)) is
// not inherited, so Python still receives the interrupt.
BOOL WINAPI serve_ctrl_handler(DWORD type) {
    return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT;
}
#endif

}  // namespace

int run_serve(int argc, char** argv) {
    ServeOptions options;
    if (!parse_options(argc, argv, options)) return 1;

    fs::path dir;
    if (!resolve_directory(options, dir)) return 1;

    Python python;
    if (!find_python(python)) return 1;

#ifndef _WIN32
    if (!probe_port(options.host, options.port)) return 1;
#endif

    // Argument vector only, never a shell string. `--opt=value` keeps a host
    // or path beginning with '-' from being read as an option by argparse;
    // -u keeps Python's request log unbuffered when stdout is a pipe.
    std::vector<std::string> args = python.prefix;
    args.insert(args.end(), {"-u", "-m", "http.server", std::to_string(options.port),
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
    if (result.exit_code != 0) {
        console::error("Python exited with status " + std::to_string(result.exit_code));
        std::cerr << "  if the port is already in use, choose another with --port N\n";
    }
    return result.exit_code;
#endif
}
