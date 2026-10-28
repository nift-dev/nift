#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
static pid_t shell_pid;
static pid_t shell_pgrp;
__attribute__((constructor)) static void init(void) { shell_pid = getpid(); shell_pgrp = getpgrp(); }
int tcsetpgrp(int fd, pid_t pgrp) {
    static int (*real)(int, pid_t);
    if (!real) real = dlsym(RTLD_NEXT, "tcsetpgrp");
    /* Return a restore error after restoring the tty, so explicit exit can
       still be read. The independent R1 reproducer separately covers a real
       failed restoration followed by EIO/EOF. */
    if (getpid() == shell_pid && pgrp == shell_pgrp) {
        (void)real(fd, pgrp);
        errno = EIO;
        return -1;
    }
    return real(fd, pgrp);
}
