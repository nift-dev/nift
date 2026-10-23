// CP16: the host native's successful open() must reach the Observer (read).
#include "rt/host/observer.h"
#include "rt/natives/host_ops.h"
#include "rt/string.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
struct Spy final : nift::rt::host::Observer {
    int reads = 0;
    std::size_t bytes = 0;
    void presence(const std::filesystem::path&, bool, std::string_view) override {}
    void read(const std::filesystem::path&, std::string_view value) override { ++reads; bytes = value.size(); }
    void failed_read(const std::filesystem::path&) override {}
    void stream_open(const std::filesystem::path&) override {}
    void streamed_read(const std::filesystem::path&, std::uint64_t, const std::uint64_t*) override {}
    void metadata_read(const std::filesystem::path&) override {}
    void written(const std::filesystem::path&) override {}
    void listing(const std::string&, const std::filesystem::path&) override {}
    void environment(const std::string&, const std::optional<std::string>&) override {}
    void environment_snapshot() override {}
    void platform(const std::string&) override {}
};
}

namespace {
#if defined(__linux__)
// N1: deterministic post-open read failure (/proc/self/mem) and open failure
// for all four read natives, at the native/Context boundary. Linux-only: the
// read-failure input is a kernel file, not an EIO device.
bool code_case(nift::rt::natives::NativeFn fn, const char* path, const char* want, const char* want_message) {
    nift::rt::natives::Context context;
    std::string code;
    context.host.error_code = &code;
    nift::rt::Value argument = nift::rt::make_string(path);
    nift::rt::Value result;
    std::string error;
    const nift::rt::natives::Status status = fn(context, &argument, 1, result, error);
    if (status != nift::rt::natives::Status::Recoverable || code != want ||
        error.find(want_message) == std::string::npos) {
        std::fprintf(stderr, "N1 native %s: status=%d code=%s error=%s (want %s)\n", path,
                     static_cast<int>(status), code.c_str(), error.c_str(), want);
        return false;
    }
    return true;
}
#endif
} // namespace

int main() {
    const std::filesystem::path path = "host-native-observer.txt";
    { std::ofstream out(path, std::ios::binary); out << "x"; }
    Spy spy;
    nift::rt::natives::Context context;
    context.host.observer = &spy;
    nift::rt::Value argument = nift::rt::make_string(path.generic_string());
    nift::rt::Value result;
    std::string error;
    const nift::rt::natives::Status status =
        nift::rt::natives::host_open(context, &argument, 1, result, error);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    if (status != nift::rt::natives::Status::Ok || spy.reads != 1 || spy.bytes != 1) {
        std::fprintf(stderr, "host observer: status=%d reads=%d error=%s\n",
                     static_cast<int>(status), spy.reads, error.c_str());
        return 1;
    }
#if defined(__linux__)
    const char* read_failure = "/proc/self/mem";
    const char* open_failure = "/nonexistent/n1-native-x";
    if (!code_case(nift::rt::natives::host_open, read_failure, "io.read_failed", "input failure") ||
        !code_case(nift::rt::natives::host_open_bytes, read_failure, "io.read_failed", "input failure") ||
        !code_case(nift::rt::natives::host_cat, read_failure, "io.read_failed", "input failure") ||
        !code_case(nift::rt::natives::host_sha256_file, read_failure, "io.read_failed", "input failure") ||
        !code_case(nift::rt::natives::host_open, open_failure, "io.open_failed", "cannot open path") ||
        !code_case(nift::rt::natives::host_open_bytes, open_failure, "io.open_failed", "cannot open path") ||
        !code_case(nift::rt::natives::host_cat, open_failure, "io.open_failed", "cannot open path") ||
        !code_case(nift::rt::natives::host_sha256_file, open_failure, "io.open_failed", "cannot open path"))
        return 1;
    std::printf("N1 native read/open code mapping: PASS\n");
#endif
    return 0;
}
