/* Malformed scanner control: embedded_defect. */
#include <stdio.h>
int main(void) {
    fputs("noise extra_fd=3 flags=0 target=/dev/tty\nexec_extra_fds=1 more noise\n", stdout);
    return 1;
}
