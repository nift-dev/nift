/* CP21 portable foreground-ownership probe used by the PTY contract. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static void report_resume(int signo) {
    (void)signo;
    const int saved = errno;
    static const char foreground[] = "RESUMED-Z-FOREGROUND\n";
    static const char background[] = "RESUMED-Z-BACKGROUND\n";
    const int owns_terminal = tcgetpgrp(STDIN_FILENO) == getpgrp();
    const ssize_t written = owns_terminal
        ? write(STDOUT_FILENO, foreground, sizeof foreground - 1)
        : write(STDOUT_FILENO, background, sizeof background - 1);
    (void)written;
    errno = saved;
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--read-and-wait") == 0) {
        char line[128];
        struct sigaction on_continue = {0};
        on_continue.sa_handler = report_resume;
        on_continue.sa_flags = SA_RESTART;
        if (sigemptyset(&on_continue.sa_mask) != 0 || sigaction(SIGCONT, &on_continue, NULL) != 0) return 1;
        puts("READY-Z");
        fflush(stdout);
        for (;;) {
            errno = 0;
            if (fgets(line, sizeof line, stdin)) break;
            if (errno != EINTR) {
                fprintf(stderr, "resume probe read failed: %s\n", feof(stdin) ? "end of file" : strerror(errno));
                return 1;
            }
            clearerr(stdin);
        }
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
