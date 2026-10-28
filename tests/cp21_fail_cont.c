/* Test-only SIGCONT failure injection; every other signal is forwarded. */
#define _GNU_SOURCE
#include <signal.h>
#include <dlfcn.h>
#include <errno.h>

int kill(pid_t pid, int sig) {
    static int (*real)(pid_t, int);
    if (!real) real = dlsym(RTLD_NEXT, "kill");
    if (sig == SIGCONT) {
        errno = EPERM;
        return -1;
    }
    return real(pid, sig);
}
