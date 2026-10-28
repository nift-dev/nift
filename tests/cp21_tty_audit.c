/* Test-only, durable kernel evidence of terminal handoff before shell cleanup.
 * No delay, failure injection, or process-lifetime modification. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
}
int tcsetpgrp(int fd, pid_t pgrp) {
    static int (*real)(int, pid_t);
    if (!real) real = dlsym(RTLD_NEXT, "tcsetpgrp");
    if (!real) { errno = ENOSYS; return -1; }
    int rc = real(fd, pgrp);
    int saved = errno;
    pid_t observed = tcgetpgrp(fd);
    record("handoff", pgrp, observed, rc);
    errno = saved;
    return rc;
}
