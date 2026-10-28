/* Test-only fallback window: force /dev/tty open to fail and delay the
 * duplicate. Intercepts both dup() (mutant path) and F_DUPFD_CLOEXEC (safe
 * path). cp21_dup_fd() reports the duplicated descriptor. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

static int armed;
static int observed = -1;

void cp21_fallback_arm(void) { armed = 1; }
int cp21_dup_fd(void) { return __atomic_load_n(&observed, __ATOMIC_ACQUIRE); }

int open(const char* path, int flags, ...) {
    static int (*real)(const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "open");
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    if (armed && strcmp(path, "/dev/tty") == 0) {
        errno = ENOENT;
        return -1;
    }
    return real(path, flags, mode);
}

int dup(int fd) {
    static int (*real)(int);
    if (!real) real = dlsym(RTLD_NEXT, "dup");
    int out = real(fd);
    if (armed && fd == STDIN_FILENO && out >= 0) {
        __atomic_store_n(&observed, out, __ATOMIC_RELEASE);
        usleep(200000);
    }
    return out;
}

int fcntl(int fd, int cmd, ...) {
    static int (*real)(int, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "fcntl");
    va_list ap;
    va_start(ap, cmd);
    if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
        int arg = va_arg(ap, int);
        va_end(ap);
        int out = real(fd, cmd, arg);
        if (armed && cmd == F_DUPFD_CLOEXEC && out >= 0) {
            __atomic_store_n(&observed, out, __ATOMIC_RELEASE);
            usleep(200000);
        }
        return out;
    }
    if (cmd == F_GETFD || cmd == F_GETFL || cmd == F_GETOWN || cmd == F_GETSIG ||
        cmd == F_GETLEASE || cmd == F_GETPIPE_SZ) {
        va_end(ap);
        return real(fd, cmd);
    }
    if (cmd == F_SETFD || cmd == F_SETFL || cmd == F_SETOWN || cmd == F_SETSIG ||
        cmd == F_SETLEASE || cmd == F_NOTIFY || cmd == F_SETPIPE_SZ) {
        int arg = va_arg(ap, int);
        va_end(ap);
        return real(fd, cmd, arg);
    }
    struct flock* lock = va_arg(ap, struct flock*);
    va_end(ap);
    return real(fd, cmd, lock);
}
