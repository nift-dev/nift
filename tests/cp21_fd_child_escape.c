/* Timeout control: a descendant escapes the scanner process group and holds
 * the capture pipe. The harness must still clean it up. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>

int main(void) {
    const pid_t child = fork();
    if (child == 0) {
        setpgid(0, 0);
        sleep(30);
        _exit(0);
    }
    printf("exec_extra_fds=0\n");
    fflush(stdout);
    _exit(0);
}
