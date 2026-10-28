#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
static unsigned char tracked[4096];
static int is_stat(const char* path) {
    if (!path || strncmp(path, "/proc/", 6) != 0) return 0;
    const char* p = path + 6;
    if (strncmp(p, "self/", 5) == 0 || strncmp(p, "thread-self/", 12) == 0) return 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') { ++p; ++digits; }
    return digits > 0 && strcmp(p, "/stat") == 0;
}
static int do_open(const char* path, int flags, mode_t mode, int (*real)(const char*, int, ...)) {
    int fd = real(path, flags, mode);
    if (fd >= 0 && fd < (int)sizeof tracked && is_stat(path)) tracked[fd] = 1;
    return fd;
}
int open(const char* path, int flags, ...) {
    static int (*real)(const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "open");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    return do_open(path, flags, mode, real);
}
int open64(const char* path, int flags, ...) {
    static int (*real)(const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "open64");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    return do_open(path, flags, mode, real);
}
int openat(int dirfd, const char* path, int flags, ...) {
    static int (*real)(int, const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "openat");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    int fd = real(dirfd, path, flags, mode);
    if (fd >= 0 && fd < (int)sizeof tracked && is_stat(path)) tracked[fd] = 1;
    return fd;
}
ssize_t read(int fd, void* buf, size_t count) {
    static ssize_t (*real)(int, void*, size_t);
    if (!real) real = dlsym(RTLD_NEXT, "read");
    if (fd >= 0 && fd < (int)sizeof tracked && tracked[fd]) { tracked[fd] = 0; return 0; }
    return real(fd, buf, count);
}
