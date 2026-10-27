/* Control-only: deny PR_SET_CHILD_SUBREAPER. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/prctl.h>

int prctl(int option, ...) {
    static int (*real)(int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "prctl");
    if (option == PR_SET_CHILD_SUBREAPER) {
        errno = EPERM;
        return -1;
    }
    va_list ap;
    va_start(ap, option);
    const unsigned long a1 = va_arg(ap, unsigned long);
    const unsigned long a2 = va_arg(ap, unsigned long);
    const unsigned long a3 = va_arg(ap, unsigned long);
    const unsigned long a4 = va_arg(ap, unsigned long);
    va_end(ap);
    return real(option, a1, a2, a3, a4);
}
