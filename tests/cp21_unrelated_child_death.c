/* Control-only unrelated failure: SIGCONT is replaced by SIGKILL. */
#define _GNU_SOURCE
#include <signal.h>
#include <unistd.h>
#include <dlfcn.h>

static int delay_input;

int kill(pid_t pid, int sig) {
    static int (*real)(pid_t, int);
    if (!real) real = dlsym(RTLD_NEXT, "kill");
    if (sig == SIGCONT) {
        int rc = real(pid, SIGKILL);
        delay_input = 1;
        return rc;
    }
    return real(pid, sig);
}

ssize_t read(int fd, void* buf, size_t n) {
    static ssize_t (*real)(int, void*, size_t);
    if (!real) real = dlsym(RTLD_NEXT, "read");
    if (fd == 0 && delay_input) {
        delay_input = 0;
        usleep(100000);
    }
    return real(fd, buf, n);
}
