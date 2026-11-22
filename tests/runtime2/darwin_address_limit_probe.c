#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

static int set_soft(rlim_t soft, rlim_t hard) {
    struct rlimit limits = {soft, hard};
    return setrlimit(RLIMIT_AS, &limits);
}

static int configure(rlim_t allowance) {
    struct rlimit inherited;
    if (getrlimit(RLIMIT_AS, &inherited) < 0) return -1;
    rlim_t cap = allowance < inherited.rlim_cur ? allowance : inherited.rlim_cur;
    rlim_t baseline = 0;
    if (set_soft(cap, inherited.rlim_max) < 0) {
        if (errno != EINVAL) return -1;
        // Darwin rejects caps below the address space already mapped at startup.
        // Find the minimum accepted cap without changing the inherited hard cap.
        rlim_t low = cap, high = inherited.rlim_cur;
        while (high - low > 1) {
            const rlim_t guess = low + (high - low) / 2;
            if (set_soft(guess, inherited.rlim_max) == 0) high = guess;
            else if (errno == EINVAL) low = guess;
            else return -1;
        }
        baseline = high;
        cap = allowance < inherited.rlim_cur - baseline ? baseline + allowance : inherited.rlim_cur;
    }
    if (set_soft(cap, cap) < 0) return -1;
    fprintf(stderr, "RLIMIT_AS baseline=%" PRIuMAX " allowance=%" PRIuMAX " applied=%" PRIuMAX "\n",
            (uintmax_t)baseline, (uintmax_t)allowance, (uintmax_t)cap);
    return 0;
}

static int check_allocations(void) {
    const size_t below = (size_t)32 << 20;
    const size_t above = (size_t)256 << 20;
    void *p = mmap(NULL, below, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) {
        perror("in-bound mmap");
        return 1;
    }
    ((volatile char *)p)[below - 1] = '\0';
    munmap(p, below);
    p = mmap(NULL, above, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p != MAP_FAILED) {
        munmap(p, above);
        fputs("over-bound mmap unexpectedly succeeded\n", stderr);
        return 1;
    }
    if (errno != ENOMEM) {
        perror("over-bound mmap");
        return 1;
    }
    puts("PASS Darwin address limit: small mapping succeeds and excessive mapping is refused after exec");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "--check") == 0) return check_allocations();
    if (argc != 2 || strcmp(argv[1], "--probe") != 0) return 2;
    if (configure((rlim_t)128 << 20) < 0) {
        perror("set address limit");
        return 1;
    }
    execl(argv[0], argv[0], "--check", (char *)NULL);
    perror("exec address-limit probe");
    return 1;
}
