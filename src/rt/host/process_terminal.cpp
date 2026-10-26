// CP21 process-wide terminal ownership, POSIX.
#ifndef _WIN32
#include "rt/host/process_terminal.h"

#include <csignal>
#include <fcntl.h>
#include <mutex>
#include <termios.h>
#include <unistd.h>

namespace nift::rt::host::posix {

namespace {

struct TerminalLease {
    std::mutex mutex;
    const void* owner = nullptr;
    // A failed release leaves a coordinator-owned recovery obligation. It does
    // not keep an owner pointer, so destroying a ProcessGroup cannot dangle.
    bool pending = false;
    bool tty_pending = false;
    bool sig_pending = false;
    struct sigaction saved_sigttou {};
    bool saved = false;
    // Stable controlling-terminal reference; independent of fd 0. Retained for
    // the lease and any pending restoration, then closed.
    int tty_fd = -1;
};

TerminalLease& lease() {
    static TerminalLease instance;
    return instance;
}

int open_tty_locked(TerminalLease& l) {
    if (l.tty_fd >= 0) return l.tty_fd;
    int fd = ::open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (fd < 0 && ::isatty(STDIN_FILENO)) {
        fd = ::dup(STDIN_FILENO);
        if (fd >= 0) ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    if (fd >= 0) l.tty_fd = fd;
    return fd;
}

void close_tty_locked(TerminalLease& l) {
    if (l.tty_fd >= 0) {
        ::close(l.tty_fd);
        l.tty_fd = -1;
    }
}

// Completes the outstanding restoration steps in order. SIGTTOU stays ignored
// until the shell owns the tty, so a retry never stops the shell.
bool complete_pending_locked(TerminalLease& l) {
    if (!l.pending) return true;
    if (l.tty_pending) {
        if (open_tty_locked(l) < 0) return false;
        if (::tcsetpgrp(l.tty_fd, ::getpgrp()) != 0) return false;
        l.tty_pending = false;
        close_tty_locked(l);
    }
    if (l.sig_pending) {
        if (l.saved) {
            if (::sigaction(SIGTTOU, &l.saved_sigttou, nullptr) != 0) return false;
            l.saved = false;
        }
        l.sig_pending = false;
    }
    l.pending = l.tty_pending || l.sig_pending;
    if (!l.pending) close_tty_locked(l);
    return !l.pending;
}

} // namespace

bool terminal_acquire(const void* owner) {
    if (!owner) return false;
    TerminalLease& l = lease();
    std::lock_guard<std::mutex> lock(l.mutex);
    if (l.pending && !complete_pending_locked(l)) return false;
    if (l.owner == owner) return true;
    if (l.owner) return false;
    if (open_tty_locked(l) < 0) return false;
    struct sigaction current {};
    if (::sigaction(SIGTTOU, nullptr, &current) != 0) {
        close_tty_locked(l);
        return false;
    }
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    if (::sigaction(SIGTTOU, &ignore, nullptr) != 0) {
        close_tty_locked(l);
        return false;
    }
    l.saved_sigttou = current;
    l.saved = true;
    l.owner = owner;
    return true;
}

bool terminal_set_foreground(pid_t pgid) {
    TerminalLease& l = lease();
    std::lock_guard<std::mutex> lock(l.mutex);
    if (!l.owner || pgid <= 0) return false;
    if (open_tty_locked(l) < 0) return false;
    return ::tcsetpgrp(l.tty_fd, pgid) == 0;
}

bool terminal_release(const void* owner) {
    if (!owner) return false;
    TerminalLease& l = lease();
    std::lock_guard<std::mutex> lock(l.mutex);
    // A pending release is a coordinator-owned recovery operation; any caller
    // may drive it, but it never transfers ownership.
    if (l.pending) return complete_pending_locked(l);
    if (l.owner != owner) return false;
    l.owner = nullptr;
#if defined(NIFT_CP21_D2_MUTANT) && NIFT_CP21_D2_MUTANT
    bool tty_ok = true;
    if (::isatty(STDIN_FILENO) && ::tcsetpgrp(STDIN_FILENO, ::getpgrp()) != 0) tty_ok = false;
    close_tty_locked(l);
    l.tty_pending = false;
#else
    bool tty_ok = true;
    if (open_tty_locked(l) < 0 || ::tcsetpgrp(l.tty_fd, ::getpgrp()) != 0) tty_ok = false;
    if (tty_ok) close_tty_locked(l);
    l.tty_pending = !tty_ok;
#endif
    bool sig_ok = true;
    if (tty_ok) {
        if (l.saved) {
            if (::sigaction(SIGTTOU, &l.saved_sigttou, nullptr) != 0) sig_ok = false;
            else l.saved = false;
        }
    }
    // If the tty could not be returned, keep SIGTTOU ignored for the retry.
    l.sig_pending = tty_ok ? !sig_ok : l.saved;
    l.pending = l.tty_pending || l.sig_pending;
    if (!l.pending) close_tty_locked(l);
    return !l.pending;
}

bool terminal_owned_by(const void* owner) {
    TerminalLease& l = lease();
    std::lock_guard<std::mutex> lock(l.mutex);
    return owner != nullptr && l.owner == owner;
}

} // namespace nift::rt::host::posix
#endif  // !_WIN32
