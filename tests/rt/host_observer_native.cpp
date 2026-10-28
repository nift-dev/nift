// CP16: the host native's successful open() must reach the Observer (read),
// and N1's per-call Host error codes must reach the native Context.
#include "rt/host/host_call.h"
#include "rt/host/host_error.h"
#include "rt/host/observer.h"
#include "rt/natives/host_file_handles.h"
#include "rt/natives/host_stream_handles.h"
#include "rt/natives/host_ops.h"
#include "rt/handles/file_resource.h"
#include "rt/string.h"
#include "rt_test.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct Spy final : nift::rt::host::Observer {
    int reads = 0;
    std::size_t bytes = 0;
    std::vector<std::string> calls;
    void presence(const std::filesystem::path& p, bool exists, std::string_view kind) override {
        calls.push_back(std::string("presence ") + p.generic_string() + (exists ? " 1 " : " 0 ") + std::string(kind));
    }
    void read(const std::filesystem::path& p, std::string_view value) override {
        ++reads; bytes = value.size();
        calls.push_back("read " + p.generic_string());
    }
    void failed_read(const std::filesystem::path& p) override { calls.push_back("failed " + p.generic_string()); }
    void stream_open(const std::filesystem::path& p) override { calls.push_back("stream_open " + p.generic_string()); }
    void streamed_read(const std::filesystem::path& p, std::uint64_t, const std::uint64_t*) override {
        calls.push_back("streamed " + p.generic_string());
    }
    void metadata_read(const std::filesystem::path& p) override { calls.push_back("metadata " + p.generic_string()); }
    void written(const std::filesystem::path& p) override { calls.push_back("written " + p.generic_string()); }
    void listing(const std::string& key, const std::filesystem::path&) override { calls.push_back("listing " + key); }
    void environment(const std::string& name, const std::optional<std::string>&) override { calls.push_back("env " + name); }
    void environment_snapshot() override { calls.push_back("snapshot"); }
    void platform(const std::string&) override { calls.push_back("platform"); }
};


// CP17: file/stream resources, alias close, forged/stale handles, slot reuse
// and context teardown through the real handle table.
bool file_stream_cases() {
    using namespace nift::rt;
    using namespace nift::rt::handles;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("cp17-handles-" + std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    auto call = [](nift::rt::natives::NativeFn fn, const nift::rt::natives::Context& context,
                   const std::vector<Value>& args, Value& out, std::string& error) {
        return fn(context, args.data(), static_cast<std::uint32_t>(args.size()), out, error);
    };
    HandleTable table;
    KindSpec file_spec;
    file_spec.name = "file";
    file_spec.cleanup = nift::rt::natives::cleanup_file;
    CHECK(table.register_typed_kind<nift::rt::handles::FileResource>(HandleKind::File, file_spec) == HandleStatus::Ok);
    KindSpec stream_spec;
    stream_spec.name = "stream";
    stream_spec.cleanup = nift::rt::natives::cleanup_stream;
    CHECK(table.register_typed_kind<nift::rt::handles::StreamResource>(HandleKind::Stream, stream_spec) ==
          HandleStatus::Ok);
    ContextId context_id = 0;
    CHECK(table.open_context(ContextKind::Script, kDetached, context_id, "script cp17") == HandleStatus::Ok);
    nift::rt::natives::Context context;
    context.handles = HandleAccess{&table, context_id};
    const std::string path = (root / "file.txt").generic_string();
    Value out;
    std::string error;

    Value file;
    CHECK(call(nift::rt::natives::file_factory, context, {make_string(path)}, file, error) ==
          nift::rt::natives::Status::Ok);
    CHECK(file.is_heap() && file.boxed_handle());
    CHECK(call(nift::rt::natives::file_open, context, {file, make_string("w")}, out, error) ==
          nift::rt::natives::Status::Ok);
    CHECK(call(nift::rt::natives::file_write, context, {file, make_string("x")}, out, error) ==
          nift::rt::natives::Status::Ok);
    CHECK(call(nift::rt::natives::file_save, context, {file}, out, error) == nift::rt::natives::Status::Ok);
    Value alias = file;
    CHECK(call(nift::rt::natives::file_close, context, {alias}, out, error) == nift::rt::natives::Status::Ok);
    CHECK(call(nift::rt::natives::file_path, context, {file}, out, error) ==
          nift::rt::natives::Status::Recoverable);
    CHECK(error.find("runtime.handle_closed") != std::string::npos);
    CHECK(call(nift::rt::natives::file_close, context, {file}, out, error) ==
          nift::rt::natives::Status::Ok);  // idempotent through the alias

    Value forged = Value::handle(HandleKind::File, file.handle_id() + 1, file.handle_generation());
    CHECK(call(nift::rt::natives::file_path, context, {forged}, out, error) ==
          nift::rt::natives::Status::Internal);

    // Slot reuse must not alias the stale handle.
    Value reused;
    CHECK(call(nift::rt::natives::file_factory, context, {make_string((root / "other.txt").generic_string())},
               reused, error) == nift::rt::natives::Status::Ok);
    CHECK(call(nift::rt::natives::file_path, context, {file}, out, error) ==
          nift::rt::natives::Status::Recoverable);
    CHECK(call(nift::rt::natives::file_path, context, {reused}, out, error) ==
          nift::rt::natives::Status::Ok);

    Value stream;
    CHECK(call(nift::rt::natives::ifstream_factory, context, {make_string(path)}, stream, error) ==
          nift::rt::natives::Status::Ok);
    CHECK(call(nift::rt::natives::stream_read_line, context, {stream}, out, error) ==
          nift::rt::natives::Status::Ok);
    CHECK(out.is(Kind::String) && string_view_of(out) == "x");
    CHECK(call(nift::rt::natives::stream_close, context, {stream}, out, error) ==
          nift::rt::natives::Status::Ok);
    CHECK(call(nift::rt::natives::stream_read_line, context, {stream}, out, error) ==
          nift::rt::natives::Status::Recoverable);

    Value output;
    CHECK(call(nift::rt::natives::ofstream_factory, context, {make_string((root / "out.txt").generic_string())},
               output, error) == nift::rt::natives::Status::Ok);
    CHECK(call(nift::rt::natives::stream_write_line, context, {output, make_string("y")}, out, error) ==
          nift::rt::natives::Status::Ok);
    CHECK(call(nift::rt::natives::stream_close, context, {output}, out, error) ==
          nift::rt::natives::Status::Ok);

    Value open_file;
    CHECK(call(nift::rt::natives::file_factory, context, {make_string((root / "teardown.txt").generic_string())},
               open_file, error) == nift::rt::natives::Status::Ok);
    CHECK(call(nift::rt::natives::file_open, context, {open_file, make_string("w")}, out, error) ==
          nift::rt::natives::Status::Ok);
    const CloseReport report = table.close_context(context_id);
    // An open file at context end is legacy's "managed file left open" failure,
    // including a clean dirty-buffer state.
    CHECK(report.cleanup_failed());
    CHECK(!report.failures.empty() && report.failures.front().code == "io.write_failed");
    CHECK(call(nift::rt::natives::file_path, context, {open_file}, out, error) ==
          nift::rt::natives::Status::Recoverable);
    std::filesystem::remove_all(root, ignored);
    std::printf("CP17 file/stream handle lifecycle: PASS\n");
    return true;
}

bool has_call(const Spy& spy, const std::string& call) {
    for (const auto& c : spy.calls) if (c == call) return true;
    return false;
}
bool has_prefix(const Spy& spy, const std::string& prefix) {
    for (const auto& c : spy.calls) if (c.rfind(prefix, 0) == 0) return true;
    return false;
}

// N2: cp records its source as read; a successful mv records the source's
// absence and no longer records move-from as a write.
bool n2_cases() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("n2-observer-" + std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "dst");
    const auto write = [&](const char* name, const char* text) {
        std::ofstream out(root / name, std::ios::binary); out << text;
    };
    write("cp-src.txt", "cp\n");
    write("mv-src.txt", "mv\n");
    write("mv-fail-src.txt", "fail\n");
    const std::string cp_src = (root / "cp-src.txt").generic_string();
    const std::string cp_dst = (root / "dst" / "cp-copy.txt").generic_string();
    const std::string mv_src = (root / "mv-src.txt").generic_string();
    const std::string mv_dst = (root / "dst" / "mv-copy.txt").generic_string();
    const std::string fail_src = (root / "mv-fail-src.txt").generic_string();
    const std::string fail_dst = (root / "missing-dir" / "moved.txt").generic_string();
    Spy spy;
    nift::rt::natives::Context context;
    context.host.observer = &spy;
    nift::rt::Value args[2];
    nift::rt::Value result;
    std::string error;
    bool ok = true;
    const auto reset = [&] { spy.calls.clear(); error.clear(); };

    args[0] = nift::rt::make_string(cp_src); args[1] = nift::rt::make_string(cp_dst);
    reset();
    if (nift::rt::natives::host_cp(context, args, 2, result, error) != nift::rt::natives::Status::Ok ||
        !has_call(spy, "stream_open " + cp_src) || !has_call(spy, "written " + cp_dst) ||
        has_prefix(spy, "failed " + cp_src)) {
        std::fprintf(stderr, "N2 cp source observation failed: %s\n", error.c_str()); ok = false;
    }

    args[0] = nift::rt::make_string(mv_src); args[1] = nift::rt::make_string(mv_dst);
    reset();
    if (nift::rt::natives::host_mv(context, args, 2, result, error) != nift::rt::natives::Status::Ok ||
        !has_call(spy, "failed " + mv_src) || !has_call(spy, "written " + mv_dst) ||
        has_call(spy, "written " + mv_src)) {
        std::fprintf(stderr, "N2 mv absence observation failed: %s\n", error.c_str()); ok = false;
    }

    args[0] = nift::rt::make_string((root / "missing.txt").generic_string());
    args[1] = nift::rt::make_string((root / "dst" / "missing-copy.txt").generic_string());
    reset();
    nift::rt::natives::host_cp(context, args, 2, result, error);
    if (!has_prefix(spy, "failed ")) {
        std::fprintf(stderr, "N2 cp missing source observation failed\n"); ok = false;
    }

    args[0] = nift::rt::make_string(fail_src); args[1] = nift::rt::make_string(fail_dst);
    reset();
    nift::rt::natives::host_mv(context, args, 2, result, error);
    if (!has_call(spy, "stream_open " + fail_src)) {
        std::fprintf(stderr, "N2 failed mv source observation failed: %s\n", error.c_str()); ok = false;
    }
    std::filesystem::remove_all(root, ignored);
    if (ok) std::printf("N2 cp/mv observer observations: PASS\n");
    return ok;
}

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
    if (std::getenv("NIFT_HOST_NATIVE_NEGATIVE") != nullptr) {
        CHECK(false);  // test-integrity negative control: must make the target fail
        return rt_test::finish("rt host native negative control");
    }
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
    if (!n2_cases()) return 1;
    if (!file_stream_cases()) return 1;

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
    return rt_test::finish("rt host native");
}
