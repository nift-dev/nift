// CP21 D3 scanner supervisor: isolates scanner execution and cleanup from the
// test driver's other children. Only descendants of this process are ever
// signalled.
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;

bool read_proc_file(const std::string& path, std::string& text, bool& disappeared, std::string& error) {
    disappeared = false;
    text.clear();
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT || errno == ESRCH) { disappeared = true; return true; }
        error = "cannot open " + path + ": " + std::strerror(errno);
        return false;
    }
    char buffer[1024];
    for (;;) {
        const ssize_t got = ::read(fd, buffer, sizeof buffer);
        if (got > 0) { text.append(buffer, static_cast<std::size_t>(got)); continue; }
        if (got == 0) break;
        if (errno == EINTR) continue;
        error = "cannot read " + path + ": " + std::strerror(errno);
        ::close(fd);
        return false;
    }
    ::close(fd);
    if (text.empty()) disappeared = true;
    return true;
}

bool proc_available() {
    DIR* dir = ::opendir("/proc");
    if (!dir) return false;
    ::closedir(dir);
    std::string text;
    bool disappeared = false;
    std::string error;
    return read_proc_file("/proc/self/stat", text, disappeared, error) && !disappeared;
}

bool descendants(pid_t root, std::set<pid_t>& found, std::string& error) {
    found.clear();
    std::map<pid_t, pid_t> parents;
    DIR* dir = ::opendir("/proc");
    if (!dir) {
        error = "cannot open /proc";
        return false;
    }
    while (dirent* entry = ::readdir(dir)) {
        char* end = nullptr;
        errno = 0;
        const long pid = std::strtol(entry->d_name, &end, 10);
        if (errno != 0 || end == entry->d_name || *end != 0 || pid <= 0) continue;
        std::string text;
        bool disappeared = false;
        if (!read_proc_file("/proc/" + std::to_string(pid) + "/stat", text, disappeared, error)) {
            ::closedir(dir);
            return false;
        }
        if (disappeared) continue;
        const std::size_t close = text.rfind(')');
        if (close == std::string::npos) {
            error = "malformed /proc/" + std::to_string(pid) + "/stat";
            ::closedir(dir);
            return false;
        }
        std::istringstream fields(text.substr(close + 1));
        std::string state;
        long ppid = 0;
        if (!(fields >> state >> ppid)) {
            error = "malformed /proc/" + std::to_string(pid) + "/stat fields";
            ::closedir(dir);
            return false;
        }
        parents[static_cast<pid_t>(pid)] = static_cast<pid_t>(ppid);
    }
    ::closedir(dir);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [pid, ppid] : parents) {
            if (pid == root || found.count(pid)) continue;
            if (ppid == root || found.count(ppid)) {
                found.insert(pid);
                changed = true;
            }
        }
    }
    return true;
}

bool reap_bounded(pid_t child, const Clock::time_point& end) {
    bool direct_reaped = child <= 0;
    while (Clock::now() < end) {
        int status = 0;
        const pid_t got = ::waitpid(-1, &status, WNOHANG);
        if (got == child) direct_reaped = true;
        if (got > 0) continue;
        if (got < 0 && errno == EINTR) continue;
        if (got < 0 && errno == ECHILD) return true;
        if (got < 0) return false;
        ::usleep(2000);
    }
    return direct_reaped;
}

bool cleanup(pid_t child, std::string& error) {
    // Collect descendants BEFORE the scanner parent dies; an escaped child is
    // still visible under its parent regardless of subreaper semantics.
    std::set<pid_t> targets;
    const bool inspected = descendants(::getpid(), targets, error);
    const pid_t pgid = ::getpgid(child);
    if (pgid == child) {
        if (::kill(-child, SIGKILL) != 0 && errno != ESRCH) {
            error = "killpg failed";
            return false;
        }
    }
    if (::kill(child, SIGKILL) != 0 && errno != ESRCH) {
        error = "kill failed";
        return false;
    }
    for (pid_t pid : targets)
        if (::kill(pid, SIGKILL) != 0 && errno != ESRCH) {
            error = "descendant kill failed";
            return false;
        }
    const auto end = Clock::now() + std::chrono::seconds(2);
    const bool reaped = reap_bounded(child, end);
    if (!reaped) {
        error = "direct child not reaped";
        return false;
    }
    if (!inspected) return false;
    while (Clock::now() < end) {
        if (!descendants(::getpid(), targets, error)) return false;
        if (targets.empty()) return true;
        for (pid_t pid : targets)
            if (::kill(pid, SIGKILL) != 0 && errno != ESRCH) {
                error = "descendant kill failed";
                return false;
            }
        if (!reap_bounded(child, end)) {
            error = "descendant reap failed";
            return false;
        }
        ::usleep(2000);
    }
    if (!descendants(::getpid(), targets, error)) return false;
    if (!targets.empty()) {
        error = "descendants survived SIGKILL";
        return false;
    }
    return true;
}

void write_status(const char* path, const std::string& text) {
    std::ofstream out(path, std::ios::trunc);
    out << text << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) return 5;
    const char* scanner = argv[1];
    const char* output_path = argv[2];
    const char* status_path = argv[3];
    const int timeout_ms = std::atoi(argv[4]);
    if (::prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0) {
        write_status(status_path, "SETUP_FAIL subreaper unavailable");
        return 3;
    }
    if (!proc_available()) {
        write_status(status_path, "SETUP_FAIL proc unavailable");
        return 3;
    }
    const int out = ::open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out < 0) {
        write_status(status_path, "SETUP_FAIL output unavailable");
        return 3;
    }
    if (::dup2(out, STDOUT_FILENO) < 0) {
        ::close(out);
        write_status(status_path, "SETUP_FAIL output redirect failed");
        return 3;
    }
    if (out != STDOUT_FILENO) ::close(out);
    const pid_t child = ::fork();
    if (child < 0) {
        write_status(status_path, "SETUP_FAIL fork failed");
        return 3;
    }
    if (child == 0) {
        ::setpgid(0, 0);
        ::execl(scanner, scanner, static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::setpgid(child, child);
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    int status = 0;
    for (;;) {
        if (Clock::now() >= deadline) {
            std::string error;
            const bool ok = cleanup(child, error);
            write_status(status_path, ok ? "TIMEOUT_CLEANED" : "TIMEOUT_CLEANUP_FAIL " + error);
            return ok ? 2 : 4;
        }
        const pid_t got = ::waitpid(child, &status, WNOHANG);
        if (got == child) break;
        if (got < 0 && errno == ECHILD) {
            write_status(status_path, "SETUP_FAIL child reaped elsewhere");
            return 3;
        }
        if (got < 0 && errno != EINTR) {
            write_status(status_path, "SETUP_FAIL waitpid failed");
            return 3;
        }
        ::usleep(2000);
    }
    // A normal scanner should not leave descendants behind. If it does, remove
    // them before reporting completion; failure is not a clean result.
    std::set<pid_t> leftovers;
    std::string error;
    if (!descendants(::getpid(), leftovers, error)) {
        write_status(status_path, "TIMEOUT_CLEANUP_FAIL " + error);
        return 4;
    }
    if (!leftovers.empty()) {
        const bool ok = cleanup(child, error);
        write_status(status_path, ok ? "COMPLETED_CLEANED_DESCENDANTS " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1)
                                     : "TIMEOUT_CLEANUP_FAIL " + error);
        return ok ? 0 : 4;
    }
    if (WIFSIGNALED(status))
        write_status(status_path, "SIGNALED " + std::to_string(WTERMSIG(status)));
    else if (WIFEXITED(status))
        write_status(status_path, "COMPLETED " + std::to_string(WEXITSTATUS(status)));
    else
        write_status(status_path, "SETUP_FAIL child status unavailable");
    return 0;
}
