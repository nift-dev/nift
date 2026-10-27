// Self-validating table for the strict D3 scanner grammar.
#include "cp21_fd_protocol.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

struct Case {
    const char* name;
    std::string output;
    bool exited;
    int exit_code;
    bool valid;
    long count;
    bool defect;
};

void check(const Case& c) {
    const auto result = cp21::parse_fd_protocol(c.output, c.exited, c.exit_code);
    const bool ok = result.valid == c.valid && (!c.valid || result.count == c.count) &&
                    (!c.valid || result.defect == c.defect);
    std::printf("%s protocol-%s", ok ? "PASS" : "FAIL", c.name);
    if (!ok) {
        std::printf(" expected valid=%d count=%ld defect=%d got valid=%d count=%ld defect=%d error=%s",
                    c.valid, c.count, c.defect, result.valid, result.count, result.defect,
                    result.error.c_str());
        ++failures;
    }
    std::printf("\n");
}

} // namespace

int main() {
    const std::string clean = "exec_extra_fds=0\n";
    const std::string defect =
        "extra_fd=3 flags=0 target=/dev/tty\nexec_extra_fds=1\n";
    const std::vector<Case> cases = {
        {"clean", clean, true, 0, true, 0, false},
        {"defect", defect, true, 1, true, 1, true},
        {"clean-exit23", clean, true, 23, false, 0, false},
        {"defect-exit0", defect, true, 0, false, 0, false},
        {"signal", clean, false, 0, false, 0, false},
        {"zero-junk", "exec_extra_fds=0junk\n", true, 0, false, 0, false},
        {"embedded-clean", "noise exec_extra_fds=0 more noise\n", true, 0, false, 0, false},
        {"embedded-defect", "noise extra_fd=3 flags=0 target=/dev/tty\nexec_extra_fds=1 more noise\n", true, 1, false, 0, false},
        {"embedded-nonzero", "exec_extra_fds=1 more noise\n", true, 1, false, 0, false},
        {"plus-zero", "exec_extra_fds=+0\n", true, 0, false, 0, false},
        {"space-zero", "exec_extra_fds= 0\n", true, 0, false, 0, false},
        {"negative", "exec_extra_fds=-1\n", true, 0, false, 0, false},
        {"huge", "exec_extra_fds=9999999999\n", true, 0, false, 0, false},
        {"leading-zero", "exec_extra_fds=00\n", true, 0, false, 0, false},
        {"missing", "extra_fd=3 flags=0 target=/dev/tty\n", true, 1, false, 0, false},
        {"duplicate", "exec_extra_fds=0\nexec_extra_fds=0\n", true, 0, false, 0, false},
        {"nonzero-no-records", "exec_extra_fds=1\n", true, 1, false, 0, false},
        {"zero-with-record", "extra_fd=3 flags=0 target=/dev/tty\nexec_extra_fds=0\n", true, 0, false, 0, false},
        {"duplicate-record",
         "extra_fd=3 flags=0 target=/dev/tty\nextra_fd=3 flags=0 target=/dev/tty\nexec_extra_fds=2\n",
         true, 1, false, 0, false},
        {"malformed-record", "extra_fd=x flags=0 target=/dev/tty\nexec_extra_fds=1\n", true, 1, false, 0, false},
        {"partial", "exec_extra_fds=\n", true, 0, false, 0, false},
        {"arbitrary", "hello\n", true, 0, false, 0, false},
        {"blank-line", "exec_extra_fds=0\n\n", true, 0, false, 0, false},
        {"fd-range", "extra_fd=2 flags=0 target=/dev/tty\nexec_extra_fds=1\n", true, 1, false, 0, false},
        {"bad-flags", "extra_fd=3 flags=2 target=/dev/tty\nexec_extra_fds=1\n", true, 1, false, 0, false},
        {"empty-target", "extra_fd=3 flags=0 target=\nexec_extra_fds=1\n", true, 1, false, 0, false},
        {"clean-extra-text", "exec_extra_fds=0\nnoise\n", true, 0, false, 0, false},
    };
    for (const Case& c : cases) check(c);
    std::printf("%s\n", failures ? "FAIL CP21 fd protocol unit"
                                  : "PASS CP21 fd protocol unit");
    return failures ? 1 : 0;
}
