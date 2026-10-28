/* Timeout control that remains in the scanner process group. */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("exec_extra_fds=0\n");
    fflush(stdout);
    sleep(30);
    return 0;
}
