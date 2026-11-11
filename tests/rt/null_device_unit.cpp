// C139b (REVIEW-C139a Low): the worker exemption covers the null device only. Paths are only stat()ed by
// null_device(); nothing here opens or writes a device.
#include "rt/natives/host_std_stream.h"

#include <cstdio>
#include <fstream>

int main(int argc, char** argv) {
    using nift::rt::natives::null_device;
    const char* scratch = argc > 1 ? argv[1] : "null-device-unit.tmp";
    { std::ofstream(scratch) << "x"; }
    struct Case { const char* path; bool want; } cases[] = {
        {"/dev/null", true},       // the null device
        {"/dev/zero", false},      // a character device, not the null device
        {"/dev/tty", false},       // a terminal device (stat only)
        {scratch, false},          // a regular file
        {"/nonexistent/x", false}, // no object
    };
    int failures = 0;
    for (const Case& c : cases) {
        const bool got = null_device(c.path);
        if (got != c.want) { std::printf("FAIL null_device(%s) = %d\n", c.path, got); ++failures; }
    }
    std::remove(scratch);
    std::printf("%s null-device unit: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
