// Exercise the normal CLI's argc/argv boundary where the OS command line
// cannot carry enough separate arguments to reach the VM's own guard.
#include "CLI.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    int count = 0;
    if (argc == 5) {
        const char* end = argv[1] + std::strlen(argv[1]);
        const auto parsed = std::from_chars(argv[1], end, count);
        if (parsed.ec == std::errc() && parsed.ptr == end && count > 0 && count <= 70000) {
            std::vector<char*> expanded(static_cast<std::size_t>(count) + 3, argv[4]);
            expanded[0] = argv[2];
            expanded[1] = argv[3];
            expanded.back() = nullptr;
            return run_cli(count + 2, expanded.data());
        }
    }
    std::fputs("argv count launcher: expected COUNT NIFT SCRIPT REPEATED_ARG\n", stderr);
    return 125;
}
