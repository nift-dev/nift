/* Control-only: lose non-root stat records and later children observations. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

static int is_self_stat(const char* path) {
    char prefix[64];
    snprintf(prefix, sizeof prefix, "/proc/%d/stat", (int)getpid());
    return strcmp(path, prefix) == 0;
}

static int denied(const char* path) {
    if (!path) return 0;
    if (is_children(path)) {
        if (seen_children) return 1;
        ++seen_children;
        return 0;
    }
    return is_stat(path) && !is_self_stat(path);
}

static int do_open(const char* path, int flags, mode_t mode, int (*real)(const char*, int, ...)) {
    if (denied(path)) { errno = ENOENT; return -1; }
    return real(path, flags, mode);
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
    if (denied(path)) { errno = ENOENT; return -1; }
    return real(dirfd, path, flags, mode);
}

int openat64(int dirfd, const char* path, int flags, ...) {
    static int (*real)(int, const char*, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "openat64");
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    if (denied(path)) { errno = ENOENT; return -1; }
    return real(dirfd, path, flags, mode);
}
