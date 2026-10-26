/* Test-only scheduling window: delay handoff to another process group. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>

int tcsetpgrp(int fd, pid_t pgrp) {
    static int (*real)(int, pid_t);
    if (!real) real = dlsym(RTLD_NEXT, "tcsetpgrp");
    if (pgrp != getpgrp()) usleep(50000);
    return real(fd, pgrp);
}
