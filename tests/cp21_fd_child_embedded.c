/* Malformed scanner control: embedded. */
#include <stdio.h>
int main(void) {
    fputs("noise exec_extra_fds=0 more noise\n", stdout);
    return 0;
}
