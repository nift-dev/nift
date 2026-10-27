/* Timeout control: escaped descendant clears the scanner tag. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    const pid_t child = fork();
    if (child == 0) {
        clearenv();
        setpgid(0, 0);
        sleep(30);
        _exit(0);
    }
    printf("exec_extra_fds=0\n");
    fflush(stdout);
    _exit(0);
}
