/* Malformed scanner control: zero_junk. */
#include <stdio.h>
int main(void) {
    fputs("exec_extra_fds=0junk\n", stdout);
    return 0;
}
