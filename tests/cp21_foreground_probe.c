/* CP21 portable foreground-ownership probe used by the PTY contract. */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--read-and-wait") == 0) {
        char line[128];
        puts("READY-Z");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) return 1;
        printf("GOT:%s", line);
        fflush(stdout);
        // Keep the same process ready to receive the second stop and final
        // interrupt. No shell-to-sleep exec races with terminal signals.
        for (;;) pause();
    }
    const int stdin_tty = isatty(STDIN_FILENO);
    const int stdout_tty = isatty(STDOUT_FILENO);
    const int stderr_tty = isatty(STDERR_FILENO);
    const pid_t pgrp = getpgrp();
    const pid_t tpgid = stdin_tty ? tcgetpgrp(STDIN_FILENO) : -1;
    printf("tty=%d,%d,%d foreground=%d\n", stdin_tty, stdout_tty, stderr_tty,
           stdin_tty && tpgid == pgrp);
    return 0;
}
