/* CP21 portable foreground-ownership probe used by the PTY contract. */
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int main(void) {
    const int stdin_tty = isatty(STDIN_FILENO);
    const int stdout_tty = isatty(STDOUT_FILENO);
    const int stderr_tty = isatty(STDERR_FILENO);
    const pid_t pgrp = getpgrp();
    const pid_t tpgid = stdin_tty ? tcgetpgrp(STDIN_FILENO) : -1;
    printf("tty=%d,%d,%d foreground=%d\n", stdin_tty, stdout_tty, stderr_tty,
           stdin_tty && tpgid == pgrp);
    return 0;
}
