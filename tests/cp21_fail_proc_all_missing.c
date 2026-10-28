/* Control-only: after startup, make both children and stat evidence unavailable. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>

static int seen_children;

static int is_children(const char* path) {
    if (!path || strncmp(path, "/proc/", 6) != 0) return 0;
    return strstr(path, "/task/") && strstr(path, "/children") &&
           strcmp(path + strlen(path) - 9, "/children") == 0;
}

static int is_stat(const char* path) {
    if (!path || strncmp(path, "/proc/", 6) != 0) return 0;
    return strcmp(path + strlen(path) - 5, "/stat") == 0;
}

static int faulted(const char* path) {
    if (is_children(path)) {
        if (seen_children++) return 1;
        return 0;
    }
    return is_stat(path);
}

static int unavailable(const char* path, int flags, mode_t mode, int (*real)(const char*, int, ...)) {
#ifdef CP21_ALL_EMPTY
    (void)path; (void)flags; (void)mode;
    return real("/dev/null", O_RDONLY);
#else
    (void)path; (void)flags; (void)mode;
    (void)real;
    errno = ENOENT;
    return -1;
#endif
}

int open(const char* path, int flags, ...) {
    static int (*real)(const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "open");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    if (faulted(path)) return unavailable(path, flags, mode, real);
    return real(path, flags, mode);
}

int open64(const char* path, int flags, ...) {
    static int (*real)(const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "open64");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    if (faulted(path)) return unavailable(path, flags, mode, real);
    return real(path, flags, mode);
}

int openat(int dirfd, const char* path, int flags, ...) {
    static int (*real)(int, const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "openat");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    if (!faulted(path)) return real(dirfd, path, flags, mode);
#ifdef CP21_ALL_EMPTY
    return real(dirfd, "/dev/null", O_RDONLY);
#else
    errno = ENOENT;
    return -1;
#endif
}

int openat64(int dirfd, const char* path, int flags, ...) {
    static int (*real)(int, const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "openat64");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    if (!faulted(path)) return real(dirfd, path, flags, mode);
#ifdef CP21_ALL_EMPTY
    return real(dirfd, "/dev/null", O_RDONLY);
#else
    errno = ENOENT;
    return -1;
#endif
}
