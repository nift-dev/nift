// Test-only FreeBSD interposer for the first rename of a specific move.
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rename(const char* source, const char* destination) {
    const char* expected_source = getenv("NIFT_TEST_EXDEV_SOURCE");
    const char* expected_destination = getenv("NIFT_TEST_EXDEV_DESTINATION");
    if (expected_source && expected_destination &&
        strcmp(source, expected_source) == 0 && strcmp(destination, expected_destination) == 0) {
        errno = EXDEV;
        return -1;
    }
    int (*system_rename)(const char*, const char*) = dlsym(RTLD_NEXT, "rename");
    if (system_rename) return system_rename(source, destination);
    errno = ENOSYS;
    return -1;
}
