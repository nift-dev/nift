/* Control-only: fail both children-file and stat inspection. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>

static int denied(const char* path) {
    if (!path || strncmp(path, "/proc/", 6) != 0) return 0;
    if (strncmp(path, "/proc/self/", 11) == 0) return 0;
    const size_t n = strlen(path);
    return (n >= 5 && strcmp(path + n - 5, "/stat") == 0) ||
           (n >= 9 && strcmp(path + n - 9, "/children") == 0);
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
