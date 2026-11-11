// C139b (REVIEW-C139a Low): the worker exemption covers the null device only. Paths are only stat()ed by
// null_device(); nothing here opens or writes a device.
#include "rt/natives/host_std_stream.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    using nift::rt::natives::null_device;
    const char* scratch = argc > 1 ? argv[1] : "null-device-unit.tmp";
    { std::ofstream(scratch) << "x"; }
    // C139c: a scratch symlink to /dev/null (only the link is created, in scratch; the device is never opened).
    const std::string link = std::string(scratch) + ".link";
    std::error_code ec;
    std::filesystem::remove(link, ec);
    std::filesystem::create_symlink("/dev/null", link, ec);
    struct Case { const char* path; bool want; } cases[] = {
        {"/dev/null", true},       // the null device
        {"/dev/zero", false},      // a character device, not the null device
        {"/dev/tty", false},       // a terminal device (stat only)
        {scratch, false},          // a regular file
        {link.c_str(), true},      // a symlink to the null device (stat follows it)
        {"/nonexistent/x", false}, // no object
    };
    int failures = 0;
    for (const Case& c : cases) {
        const bool got = null_device(c.path);
        if (got != c.want) { std::printf("FAIL null_device(%s) = %d\n", c.path, got); ++failures; }
    }
    std::remove(scratch);
    std::filesystem::remove(link, ec);
    std::printf("%s null-device unit: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
