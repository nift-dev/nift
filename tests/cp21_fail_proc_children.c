/* Control-only: fail /proc/<pid>/task/<pid>/children enumeration. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>

static int denied(const char* path) {
    if (!path || strncmp(path, "/proc/", 6) != 0) return 0;
    const char* p = path + 6;
    if (strncmp(p, "self/", 5) == 0 || strncmp(p, "thread-self/", 12) == 0) return 0;
    return strstr(p, "/task/") && strstr(p, "/children") &&
           strcmp(p + strlen(p) - 9, "/children") == 0;
}

static int open_common(const char* path, int flags, mode_t mode, int (*real)(const char*, int, ...)) {
    if (denied(path)) { errno = EACCES; return -1; }
    return real(path, flags, mode);
}

int open(const char* path, int flags, ...) {
    static int (*real)(const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "open");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    return open_common(path, flags, mode, real);
}
int open64(const char* path, int flags, ...) {
    static int (*real)(const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "open64");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    return open_common(path, flags, mode, real);
}
int openat(int dirfd, const char* path, int flags, ...) {
    static int (*real)(int, const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "openat");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    if (denied(path)) { errno = EACCES; return -1; }
    return real(dirfd, path, flags, mode);
}
int openat64(int dirfd, const char* path, int flags, ...) {
    static int (*real)(int, const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "openat64");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    if (denied(path)) { errno = EACCES; return -1; }
    return real(dirfd, path, flags, mode);
}
