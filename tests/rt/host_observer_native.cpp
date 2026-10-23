// CP16: the host native's successful open() must reach the Observer (read),
// and N1's per-call Host error codes must reach the native Context.
#include "rt/host/host_call.h"
#include "rt/host/host_error.h"
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

// N1: the native/Context boundary for one read native and one failure path.
bool code_case(nift::rt::natives::NativeFn fn, const std::string& path, const char* want,
               const char* want_message) {
    nift::rt::natives::Context context;
    std::string code;
    context.host.error_code = &code;
    nift::rt::Value argument = nift::rt::make_string(path);
    nift::rt::Value result;
    std::string error;
    const nift::rt::natives::Status status = fn(context, &argument, 1, result, error);
    if (status != nift::rt::natives::Status::Recoverable || code != want ||
        error.find(want_message) == std::string::npos) {
        std::fprintf(stderr, "N1 native %s: status=%d code=%s error=%s (want %s)\n", path.c_str(),
                     static_cast<int>(status), code.c_str(), error.c_str(), want);
        return false;
    }
    return true;
}

// N1: HostCall is the portable sink/reset/classification seam; no production
// fault injection is needed for these checks.
bool sink_cases() {
    nift::rt::host::HostCallEnv env;
    std::string code = "stale";
    env.error_code = &code;
    {
        nift::rt::host::HostCall call(env, nift::rt::host::HostOp::Read, "n1-unit");
        if (!code.empty()) {
            std::fprintf(stderr, "N1 sink: constructor did not clear %s\n", code.c_str());
            return false;
        }
        nift::rt::host::HostError error;
        nift::rt::host::make_error(error, "io.read_failed", "n1-unit");
        if (call.fail(nift::rt::host::HostStatus::Recoverable, error) !=
                nift::rt::host::HostStatus::Recoverable ||
            code != "io.read_failed") {
            std::fprintf(stderr, "N1 sink: read failure wrote %s\n", code.c_str());
            return false;
        }
    }
    code = "stale";
    {
        nift::rt::host::HostCall call(env, nift::rt::host::HostOp::Read, "n1-unit");
        if (!code.empty()) {
            std::fprintf(stderr, "N1 sink: second call did not reset\n");
            return false;
        }
        nift::rt::host::HostError error;
        nift::rt::host::make_error(error, "io.read_failed", "n1-unit");
        // A code reported with the wrong status is a classification defect.
        if (call.fail(nift::rt::host::HostStatus::Fatal, error) != nift::rt::host::HostStatus::Internal ||
            code != "internal.host_call") {
            std::fprintf(stderr, "N1 sink: classification mismatch wrote %s\n", code.c_str());
            return false;
        }
    }
    code = "stale";
    {
        nift::rt::host::HostCall call(env, nift::rt::host::HostOp::Read, "n1-unit");
        nift::rt::host::HostError error;
        nift::rt::host::make_error(error, "io.open_failed", "n1-unit");
        if (call.fail(nift::rt::host::HostStatus::Recoverable, error) !=
                nift::rt::host::HostStatus::Recoverable ||
            code != "io.open_failed") {
            std::fprintf(stderr, "N1 sink: open failure wrote %s\n", code.c_str());
            return false;
        }
    }
    std::printf("N1 HostCall sink/reset/classification: PASS\n");
    return true;
}
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
    if (!sink_cases()) return 1;

    // Portable open-failure mapping for all four read natives.
    std::filesystem::path missing = std::filesystem::temp_directory_path() / "n1-native-missing";
    std::filesystem::remove(missing, ignored);
    const std::string missing_text = missing.generic_string();
    if (!code_case(nift::rt::natives::host_open, missing_text, "io.open_failed", "cannot open path") ||
        !code_case(nift::rt::natives::host_open_bytes, missing_text, "io.open_failed", "cannot open path") ||
        !code_case(nift::rt::natives::host_cat, missing_text, "io.open_failed", "cannot open path") ||
        !code_case(nift::rt::natives::host_sha256_file, missing_text, "io.open_failed", "cannot open path"))
        return 1;
    std::printf("N1 native open-failure code mapping (portable): PASS\n");

#if defined(__linux__)
    // Deterministic post-open read failure: /proc/self/mem. This is a kernel
    // fixture, not an EIO device, and is mandatory on Linux when present.
    if (std::filesystem::exists("/proc/self/mem")) {
        const std::string read_failure = "/proc/self/mem";
        if (!code_case(nift::rt::natives::host_open, read_failure, "io.read_failed", "input failure") ||
            !code_case(nift::rt::natives::host_open_bytes, read_failure, "io.read_failed", "input failure") ||
            !code_case(nift::rt::natives::host_cat, read_failure, "io.read_failed", "input failure") ||
            !code_case(nift::rt::natives::host_sha256_file, read_failure, "io.read_failed", "input failure"))
            return 1;
        std::printf("N1 native post-open read-failure code mapping (Linux): PASS\n");
    } else {
        std::printf("N1 native post-open read-failure code mapping: SKIP (Linux /proc/self/mem unavailable)\n");
    }
#else
    std::printf("N1 native post-open read-failure code mapping: SKIP (Linux-only fixture)\n");
#endif
    return 0;
}
