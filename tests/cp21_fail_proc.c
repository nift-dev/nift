/* Control-only: make /proc enumeration unavailable. */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>

DIR* opendir(const char* path) {
    static DIR* (*real)(const char*);
    if (!real) real = dlsym(RTLD_NEXT, "opendir");
    if (strcmp(path, "/proc") == 0) {
        errno = ENOENT;
        return 0;
    }
    return real(path);
}
