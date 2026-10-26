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
    // A failed release leaves a coordinator-owned recovery obligation. It does
    // not keep an owner pointer, so destroying a ProcessGroup cannot dangle.
    bool pending = false;
    bool tty_pending = false;
    bool sig_pending = false;
    struct sigaction saved_sigttou {};
    bool saved = false;
};

TerminalLease& lease() {
    static TerminalLease instance;
    return instance;
}

// Completes the outstanding restoration steps in order. SIGTTOU stays ignored
// until the shell owns the tty, so a retry never stops the shell.
bool complete_pending_locked(TerminalLease& l) {
    if (!l.pending) return true;
    if (l.tty_pending) {
        if (::isatty(STDIN_FILENO) && ::tcsetpgrp(STDIN_FILENO, ::getpgrp()) != 0) return false;
        l.tty_pending = false;
    }
    if (l.sig_pending) {
        if (l.saved) {
            if (::sigaction(SIGTTOU, &l.saved_sigttou, nullptr) != 0) return false;
            l.saved = false;
        }
        l.sig_pending = false;
    }
    l.pending = l.tty_pending || l.sig_pending;
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
    // A pending release is a coordinator-owned recovery operation; any caller
    // may drive it, but it never transfers ownership.
    if (l.pending) return complete_pending_locked(l);
    if (l.owner != owner) return false;
    l.owner = nullptr;
    bool tty_ok = true;
    if (::isatty(STDIN_FILENO) && ::tcsetpgrp(STDIN_FILENO, ::getpgrp()) != 0) tty_ok = false;
    l.tty_pending = !tty_ok;
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
    return !l.pending;
}

bool terminal_owned_by(const void* owner) {
    TerminalLease& l = lease();
    std::lock_guard<std::mutex> lock(l.mutex);
    return owner != nullptr && l.owner == owner;
}

} // namespace nift::rt::host::posix
#endif  // !_WIN32
