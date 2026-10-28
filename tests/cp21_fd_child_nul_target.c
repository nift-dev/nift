/* Malformed target control: descriptor target contains a NUL byte. */
#include <sys/types.h>
#include <unistd.h>

int main(void) {
    static const char output[] =
        "extra_fd=3 flags=0 target=\0junk\nexec_extra_fds=1\n";
    const ssize_t written = write(STDOUT_FILENO, output, sizeof output - 1);
    (void)written;
    return 1;
}
