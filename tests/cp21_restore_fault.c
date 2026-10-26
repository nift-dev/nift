/* Test-only terminal-operation fault injection.
 * cp21_arm(1) fails one SIGTTOU restoration; 4 fails persistently.
 * cp21_arm(2) fails one return-to-shell tcsetpgrp; 5 fails persistently.
 * cp21_arm(3) rejects every foreign-group tcsetpgrp.
 * The CP21_FAIL_RESTORE/CP21_FAIL_HANDOFF environment forms arm once at
 * startup for CLI probes. */
#define _GNU_SOURCE
#include <signal.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>

static int armed;
static pid_t shell_pgrp;
static pid_t injecting_pid;

__attribute__((constructor)) static void init(void) {
    if (getenv("CP21_FAIL_RESTORE")) {
        armed = 1;
        injecting_pid = getpid();
    } else if (getenv("CP21_FAIL_HANDOFF")) {
        armed = 3;
        shell_pgrp = getpgrp();
    }
}

void cp21_arm(int value) {
    armed = value;
    shell_pgrp = getpgrp();
    injecting_pid = getpid();
}

int sigaction(int sig, const struct sigaction* act, struct sigaction* old) {
    static int (*real)(int, const struct sigaction*, struct sigaction*);
    if (!real) real = dlsym(RTLD_NEXT, "sigaction");
    if ((armed == 1 || armed == 4) && (!injecting_pid || injecting_pid == getpid()) &&
        sig == SIGTTOU && act && act->sa_handler != SIG_IGN) {
        if (armed == 1) armed = 0;
        errno = EIO;
        return -1;
    }
    return real(sig, act, old);
}

int tcsetpgrp(int fd, pid_t pgid) {
    static int (*real)(int, pid_t);
    if (!real) real = dlsym(RTLD_NEXT, "tcsetpgrp");
    if ((armed == 2 || armed == 5) && pgid == getpgrp()) {
        if (armed == 2) armed = 0;
        errno = EIO;
        return -1;
    }
    if (armed == 3 && pgid != shell_pgrp) {
        errno = EIO;
        return -1;
    }
    return real(fd, pgid);
}
