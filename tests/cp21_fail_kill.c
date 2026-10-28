/* Control-only: deny SIGKILL. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>

int kill(pid_t pid, int sig) {
    static int (*real)(pid_t, int);
    if (!real) real = dlsym(RTLD_NEXT, "kill");
    if (sig == SIGKILL) { errno = EPERM; return -1; }
    return real(pid, sig);
}
