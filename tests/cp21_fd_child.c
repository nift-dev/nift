/* Exec witness: report every inherited descriptor above stdio. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int extra = 0;
    for (int fd = 3; fd < 512; ++fd) {
        if (fcntl(fd, F_GETFD) < 0) continue;
        char path[80];
        char target[512];
        snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
        const ssize_t len = readlink(path, target, sizeof target - 1);
        target[len < 0 ? 0 : len] = 0;
        printf("extra_fd=%d flags=%d target=%s\n", fd, fcntl(fd, F_GETFD), target);
        ++extra;
    }
    printf("exec_extra_fds=%d\n", extra);
    return extra == 0 ? 0 : 1;
}
