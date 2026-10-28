/* Unrelated failure control: clean scan, but exit 23. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int main(void) {
    int extra = 0;
    for (int fd = 3; fd < 512; ++fd) {
        if (fcntl(fd, F_GETFD) < 0) continue;
        ++extra;
    }
    printf("exec_extra_fds=%d\n", extra);
    return 23;
}
