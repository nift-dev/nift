// CP21 process-wide terminal ownership, POSIX.
#ifndef _WIN32
#include "rt/host/process_terminal.h"

#include <csignal>
#include <mutex>
#include <termios.h>
#include <unistd.h>

namespace nift::rt::host::posix {

namespace {

struct TerminalLease {
    std::mutex mutex;
    const void* owner = nullptr;
    struct sigaction saved_sigttou {};
    bool saved = false;
};

TerminalLease& lease() {
    static TerminalLease instance;
    return instance;
}

} // namespace

bool terminal_acquire(const void* owner) {
    if (!owner) return false;
    TerminalLease& l = lease();
    std::lock_guard<std::mutex> lock(l.mutex);
    if (l.owner == owner) return true;
    if (l.owner) return false;
    struct sigaction current {};
    if (::sigaction(SIGTTOU, nullptr, &current) != 0) return false;
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    if (::sigaction(SIGTTOU, &ignore, nullptr) != 0) return false;
    l.saved_sigttou = current;
    l.saved = true;
    l.owner = owner;
    return true;
}

bool terminal_release(const void* owner) {
    if (!owner) return false;
    TerminalLease& l = lease();
    std::lock_guard<std::mutex> lock(l.mutex);
    if (l.owner != owner) return false;
    bool ok = true;
    if (::isatty(STDIN_FILENO) && ::tcsetpgrp(STDIN_FILENO, ::getpgrp()) != 0) ok = false;
    if (l.saved) {
        if (::sigaction(SIGTTOU, &l.saved_sigttou, nullptr) != 0) ok = false;
        l.saved = false;
    }
    l.owner = nullptr;
    return ok;
}

bool terminal_owned_by(const void* owner) {
    TerminalLease& l = lease();
    std::lock_guard<std::mutex> lock(l.mutex);
    return owner != nullptr && l.owner == owner;
}

} // namespace nift::rt::host::posix
#endif  // !_WIN32
