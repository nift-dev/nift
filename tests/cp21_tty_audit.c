/* Test-only, durable kernel evidence of terminal handoff before shell cleanup.
 * No delay, failure injection, or process-lifetime modification. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __APPLE__
#include <stdatomic.h>
#include <sys/wait.h>
#endif

static void record(const char* kind, pid_t requested, pid_t observed, int rc) {
    const char* path = getenv("CP21_TTY_AUDIT");
    if (!path) return;
    int saved = errno;
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd >= 0) {
        char line[160];
        int n = snprintf(line, sizeof line, "%s %ld %ld %ld %ld %d\n", kind,
                         (long)getpid(), (long)getpgrp(), (long)requested, (long)observed, rc);
        if (n > 0 && n < (int)sizeof line) (void)write(fd, line, (size_t)n);
        close(fd);
    }
    errno = saved;
}
__attribute__((constructor)) static void ready(void) {
    record("ready", 0, tcgetpgrp(STDIN_FILENO), 0);
#ifdef __APPLE__
    // This dylib audits the Nift process. Do not inject it into its external
    // commands, some of which reject DYLD_INSERT_LIBRARIES.
    if (getenv("CP21_TTY_AUDIT")) unsetenv("DYLD_INSERT_LIBRARIES");
#endif
}
#ifdef __APPLE__
// dyld binds references from the executable through this interpose entry.
static int audit_tcsetpgrp(int fd, pid_t pgrp) {
    int rc = tcsetpgrp(fd, pgrp);
#else
int tcsetpgrp(int fd, pid_t pgrp) {
    static int (*real)(int, pid_t);
    if (!real) real = dlsym(RTLD_NEXT, "tcsetpgrp");
    if (!real) { errno = ENOSYS; return -1; }
    int rc = real(fd, pgrp);
#endif
    int saved = errno;
    pid_t observed = tcgetpgrp(fd);
    record("handoff", pgrp, observed, rc);
    errno = saved;
    return rc;
}
#ifdef __APPLE__
__attribute__((used, section("__DATA,__interpose")))
static const struct {
    int (*replacement)(int, pid_t);
    int (*original)(int, pid_t);
} tty_interpose = {audit_tcsetpgrp, tcsetpgrp};

// Capture the first shell input attempt after the kernel reports a stopped
// child. Cleanup may restore the tty later and hide a failed hand-back.
static _Atomic int stopped;
static int audit_waitid(idtype_t type, id_t id, siginfo_t* info, int options) {
    int rc = waitid(type, id, info, options);
    int saved = errno;
    if (rc == 0 && info && info->si_pid > 0 && info->si_code == CLD_STOPPED) {
        pid_t group = getpgid(info->si_pid);
        if (group > 0) {
            record("stopped", group, tcgetpgrp(STDIN_FILENO), 0);
            atomic_store(&stopped, 1);
        }
    }
    errno = saved;
    return rc;
}

static ssize_t audit_read(int fd, void* buffer, size_t size) {
    if (fd == STDIN_FILENO && atomic_exchange(&stopped, 0)) {
        int saved = errno;
        record("input", 0, tcgetpgrp(fd), 0);
        errno = saved;
    }
    return read(fd, buffer, size);
}

__attribute__((used, section("__DATA,__interpose")))
static const struct {
    int (*replacement)(idtype_t, id_t, siginfo_t*, int);
    int (*original)(idtype_t, id_t, siginfo_t*, int);
} stop_interpose = {audit_waitid, waitid};

__attribute__((used, section("__DATA,__interpose")))
static const struct {
    ssize_t (*replacement)(int, void*, size_t);
    ssize_t (*original)(int, void*, size_t);
} read_interpose = {audit_read, read};
#endif
