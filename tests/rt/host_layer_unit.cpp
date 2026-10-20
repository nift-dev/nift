// CP16 Host layer unit tests: error table, op table, trace v1/v2 formats and
// emission points, HostCall classification, recording hooks, path
// resolution, run() options (alternative A) and the temporary handle adapter.
#include "rt/host/host_call.h"
#include "rt/host/host_error.h"
#include "rt/host/host_op.h"
#include "rt/host/host_trace.h"
#include "rt/host/path.h"
#include "rt/host/process.h"
#include "rt/host/process_runtime.h"
#include "rt/host/run_options.h"
#include "rt/host/run_request.h"
#include "rt_test.h"

#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdlib.h>
#include <map>
#include <string>
#include <vector>

using namespace nift::rt::host;

namespace {

struct SpySink : TraceSink {
    std::vector<std::string> lines;
    std::string ctx = "script";
    void write_line(const std::string& line) override { lines.push_back(line); }
    std::string context() override { return ctx; }
};

struct SpyObserver : Observer {
    std::vector<std::string> calls;
    void presence(const std::filesystem::path& p, bool exists, std::string_view kind) override {
        calls.push_back("presence " + p.generic_string() + " " + (exists ? "1 " : "0 ") + std::string(kind));
    }
    void read(const std::filesystem::path& p, std::string_view bytes) override { calls.push_back("read " + p.generic_string() + " " + std::to_string(bytes.size())); }
    void failed_read(const std::filesystem::path& p) override { calls.push_back("failed " + p.generic_string()); }
    void stream_open(const std::filesystem::path& p) override { calls.push_back("stream_open " + p.generic_string()); }
    void streamed_read(const std::filesystem::path& p, std::uint64_t size, const std::uint64_t*) override { calls.push_back("streamed " + p.generic_string() + " " + std::to_string(size)); }
    void metadata_read(const std::filesystem::path& p) override { calls.push_back("metadata " + p.generic_string()); }
    void written(const std::filesystem::path& p) override { calls.push_back("written " + p.generic_string()); }
    void listing(const std::string& key, const std::filesystem::path&) override { calls.push_back("listing " + key); }
    void environment(const std::string& name, const std::optional<std::string>& v) override { calls.push_back("env " + name + (v ? "=" + *v : "")); }
    void environment_snapshot() override { calls.push_back("snapshot"); }
    void platform(const std::string& v) override { calls.push_back("platform " + v); }
};

PathPolicy script_policy() {
    PathPolicy p;
    p.standalone = true;
    p.base = "/work/dir";
    p.getenv = [](const char* name) -> const char* { return std::strcmp(name, "HOME") == 0 ? "/home/u" : nullptr; };
    return p;
}

void test_error_table() {
    CHECK(catchable("io.open_failed"));
    CHECK(catchable("stream.flush_failed"));
    CHECK(catchable("json.parse_failed"));
    CHECK(!catchable("limit.exceeded"));
    CHECK(!catchable("runtime.invalid_argument"));
    CHECK(!catchable("process.detach_in_build"));
    CHECK(!catchable("no.such_code"));
    CHECK(status_for_code("io.write_failed") == HostStatus::Recoverable);
    CHECK(status_for_code("limit.exceeded") == HostStatus::Limit);
    CHECK(status_for_code("internal.host_call") == HostStatus::Internal);
    CHECK(status_for_code("runtime.invalid_operation") == HostStatus::Fatal);
    CHECK(status_for_code("unknown.code") == HostStatus::Internal);
    HostError e;
    CHECK(make_error(e, "io.open_failed", "open: cannot open path", 2) == HostStatus::Recoverable);
    CHECK(e.code == "io.open_failed" && e.message == "open: cannot open path" && e.sys_error == 2);
    // every catchable code is Recoverable and every Recoverable code is catchable
    for (const char* code : {"io.open_failed", "io.read_failed", "io.create_failed", "io.write_failed", "io.atomic_replace_failed",
                             "io.copy_failed", "io.move_failed", "io.remove_failed", "io.directory_read_failed",
                             "io.change_directory_failed", "io.metadata_failed", "stream.open_failed", "stream.read_failed",
                             "stream.write_failed", "stream.flush_failed", "stream.close_failed"}) {
        CHECK(catchable(code));
        CHECK(status_for_code(code) == HostStatus::Recoverable);
    }
}

void test_op_table() {
    for (int i = 0; i < static_cast<int>(HostOp::Count_); ++i) {
        const auto& info = op_info(static_cast<HostOp>(i));
        CHECK(static_cast<int>(info.op) == i);
        CHECK(info.v2_name && *info.v2_name);
        CHECK((info.v1_name == nullptr) == (info.v1_point == TracePoint::None));
    }
    // design §2.4 v1 timing column
    CHECK(op_info(HostOp::Read).v1_point == TracePoint::PostOk);
    CHECK(op_info(HostOp::Stat).v1_point == TracePoint::PostAlways);
    CHECK(op_info(HostOp::List).v1_point == TracePoint::Pre);
    CHECK(op_info(HostOp::Glob).v1_point == TracePoint::Pre);
    CHECK(op_info(HostOp::Open).v1_point == TracePoint::Pre);
    for (HostOp w : {HostOp::Save, HostOp::Touch, HostOp::Remove, HostOp::Mkdir, HostOp::CopyTo, HostOp::MoveTo, HostOp::MoveFrom})
        CHECK(op_info(w).v1_point == TracePoint::Pre && op_is_write(w));
    CHECK(op_info(HostOp::Getenv).v1_point == TracePoint::PostOk);
    CHECK(op_info(HostOp::GetenvTemplate).v1_point == TracePoint::PostAlways);
    CHECK(op_info(HostOp::EnvAll).v1_point == TracePoint::Pre);
    CHECK(op_info(HostOp::Spawn).v1_point == TracePoint::Pre);
    CHECK(op_info(HostOp::ReadBytes).v1_point == TracePoint::PostOk && std::string(op_info(HostOp::ReadBytes).v1_name) == "read");
    CHECK(op_info(HostOp::ReadBytes).recording == Recording::StreamedRead);
    for (HostOp u : {HostOp::StreamOpen, HostOp::StreamWrite, HostOp::Inject, HostOp::FileCopy, HostOp::FileMove,
                     HostOp::FileRemove, HostOp::Chdir, HostOp::Setenv, HostOp::Unsetenv, HostOp::Which, HostOp::Clock, HostOp::Sleep, HostOp::Random})
        CHECK(op_info(u).v1_point == TracePoint::None);
    CHECK(std::string(op_info(HostOp::CopyTo).v1_name) == "copy-to");
    CHECK(std::string(op_info(HostOp::GetenvTemplate).v1_name) == "getenv");
}

void test_trace_format() {
    // byte-identical to src/HostTrace.h emit() (legacy v1)
    CHECK(TraceChannel::format_v1("script", "read", "/a/b\"c", "12") ==
          "{\"ctx\":\"script\",\"op\":\"read\",\"arg\":\"/a/b\\\"c\",\"res\":\"12\"}\n");
    CHECK(TraceChannel::format_v1("p\n", "stat", "x\x01", "") == "{\"ctx\":\"p\\n\",\"op\":\"stat\",\"arg\":\"x\\u0001\",\"res\":\"\"}\n");
    TraceV2Record r;
    r.op = "save";
    r.arg = "/abs/x";
    r.res = "error";
    r.err = "io.write_failed";
    CHECK(TraceChannel::format_v2("script", r) == "{\"v\":2,\"ctx\":\"script\",\"op\":\"save\",\"arg\":\"/abs/x\",\"res\":\"error\",\"err\":\"io.write_failed\"}\n");
    TraceV2Record s;
    s.op = "spawn";
    s.arg = "sh";
    s.res = "launched";
    s.has_argv = true;
    s.argv = {"sh", "-c", "a b"};
    s.has_cwd = true;
    s.cwd = "/w";
    CHECK(TraceChannel::format_v2("script", s) ==
          "{\"v\":2,\"ctx\":\"script\",\"op\":\"spawn\",\"arg\":\"sh\",\"res\":\"launched\",\"argv\":[\"sh\",\"-c\",\"a b\"],\"cwd\":\"/w\"}\n");
    CHECK(TraceChannel::parse_version(nullptr) == TraceChannel::V1);
    CHECK(TraceChannel::parse_version("") == TraceChannel::V1);
    CHECK(TraceChannel::parse_version("1") == TraceChannel::V1);
    CHECK(TraceChannel::parse_version("2") == TraceChannel::V2);
    CHECK(TraceChannel::parse_version("3") == -1);
    CHECK(TraceChannel::parse_version("2x") == -1);
    SpySink sink;
    TraceChannel off(TraceChannel::Off, &sink);
    off.emit_v1("read", "x", "1");
    off.emit_v2(r);
    CHECK(sink.lines.empty());
    TraceChannel nosink(TraceChannel::V2, nullptr);
    CHECK(nosink.version() == TraceChannel::Off);
}

// Runs one op through HostCall on both channels and returns the lines.
struct Run { std::vector<std::string> v1, v2; std::vector<std::string> recorded; HostStatus status; };

Run run_op(HostOp op, const char* builtin, const std::string& raw, bool ok, const char* code = "io.open_failed",
           HostStatus st = HostStatus::Recoverable, const std::string& res = "", const Observation& obs = Observation()) {
    Run out{};
    for (int version : {1, 2}) {
        SpySink sink;
        SpyObserver observer;
        TraceChannel channel(version, &sink);
        PathPolicy policy = script_policy();
        HostCallEnv env{&channel, &observer, &policy};
        {
            HostCall call(env, op, builtin);
            CHECK(call.resolve(raw) == HostStatus::Ok);
            call.begin();
            if (version == 1) out.v1 = sink.lines;  // lines present before the attempt
            if (ok) out.status = call.succeed(res, obs);
            else {
                HostError e;
                make_error(e, code, std::string(builtin) + ": failed");
                out.status = call.fail(st, e);
            }
        }
        if (version == 1) {
            out.v1.push_back("--after--");
            for (const auto& l : sink.lines) out.v1.push_back(l);
            out.recorded = observer.calls;
        } else out.v2 = sink.lines;
    }
    return out;
}

void test_hostcall_points() {
    // read: v1 after success only; nothing on failure
    auto r = run_op(HostOp::Read, "open", "data.txt", true, nullptr, HostStatus::Ok, "5");
    CHECK(r.v1.size() == 2 && r.v1[0] == "--after--");
    CHECK(r.v1[1] == "{\"ctx\":\"script\",\"op\":\"read\",\"arg\":\"/work/dir/data.txt\",\"res\":\"5\"}\n");
    CHECK(r.v2.size() == 1 && r.v2[0] == "{\"v\":2,\"ctx\":\"script\",\"op\":\"read\",\"arg\":\"/work/dir/data.txt\",\"res\":\"5\"}\n");
    auto rf = run_op(HostOp::Read, "open", "missing.txt", false);
    CHECK(rf.status == HostStatus::Recoverable);
    CHECK(rf.v1.size() == 1);  // only the marker: no v1 line for a failed read
    CHECK(rf.v2.size() == 1 && rf.v2[0].find("\"res\":\"error\",\"err\":\"io.open_failed\"") != std::string::npos);
    CHECK(rf.recorded.size() == 1 && rf.recorded[0] == "failed /work/dir/missing.txt");  // D-028: absent/unreadable
    // writes: v1 BEFORE the attempt, with empty res; a failed save is distinguishable only in v2 (Q-H10)
    auto w = run_op(HostOp::Save, "save", "out/../o.txt", false, "io.write_failed");
    CHECK(w.v1.size() == 3 && w.v1[0] == "{\"ctx\":\"script\",\"op\":\"save\",\"arg\":\"/work/dir/o.txt\",\"res\":\"\"}\n" && w.v1[1] == "--after--");
    CHECK(w.v2.size() == 1 && w.v2[0] == "{\"v\":2,\"ctx\":\"script\",\"op\":\"save\",\"arg\":\"/work/dir/o.txt\",\"res\":\"error\",\"err\":\"io.write_failed\"}\n");
    auto wok = run_op(HostOp::Save, "save", "o.txt", true);
    CHECK(wok.v2.size() == 1 && wok.v2[0].find("\"res\":\"ok\"") != std::string::npos);
    CHECK(wok.recorded.size() == 1 && wok.recorded[0] == "written /work/dir/o.txt");
    // stat: after the query, also on error (res "error")
    Observation so;
    so.exists = true;
    so.kind = "file";
    so.metadata = true;
    auto st = run_op(HostOp::Stat, "stat", "f", true, nullptr, HostStatus::Ok, "file", so);
    CHECK(st.v1.size() == 2 && st.v1[1].find("\"op\":\"stat\"") != std::string::npos && st.v1[1].find("\"res\":\"file\"") != std::string::npos);
    CHECK(st.recorded.size() == 2 && st.recorded[0] == "presence /work/dir/f 1 file" && st.recorded[1] == "metadata /work/dir/f");
    auto ste = run_op(HostOp::Stat, "exists", "f", false, "io.metadata_failed");
    CHECK(ste.v1.size() == 2 && ste.v1[1].find("\"res\":\"error\"") != std::string::npos);
    // untraced in v1, traced in v2
    auto u = run_op(HostOp::StreamOpen, "ifstream", "b.bin", true, nullptr, HostStatus::Ok, "");
    CHECK(u.v1.size() == 1);
    CHECK(u.v2.size() == 1 && u.v2[0].find("\"op\":\"stream-open\"") != std::string::npos);
    // open_bytes/sha256_file: v1 `read` after success (host-deps), v2 read-bytes
    auto rb = run_op(HostOp::ReadBytes, "open_bytes", "b.bin", true, nullptr, HostStatus::Ok, "3");
    CHECK(rb.v1.size() == 2 && rb.v1[1].find("\"op\":\"read\"") != std::string::npos && rb.v1[1].find("\"res\":\"3\"") != std::string::npos);
    CHECK(rb.v2.size() == 1 && rb.v2[0].find("\"op\":\"read-bytes\"") != std::string::npos);
    // ~ expands in scripts
    auto t = run_op(HostOp::Touch, "touch", "~/x", true);
    CHECK(t.v1[0].find("\"arg\":\"/home/u/x\"") != std::string::npos);
}

void test_hostcall_classification() {
    SpySink sink;
    TraceChannel channel(TraceChannel::V2, &sink);
    HostCallEnv env{&channel, nullptr, nullptr};
    {
        HostCall call(env, HostOp::Remove, "rm");
        call.set_arg("/x");
        call.begin();
        HostError e;
        make_error(e, "io.remove_failed", "rm: denied");
        // A catchable code reported as fatal is a classification bug -> Internal.
        CHECK(call.fail(HostStatus::Fatal, e) == HostStatus::Internal);
        CHECK(call.error().code == "internal.host_call");
    }
    {
        HostCall call(env, HostOp::Remove, "rm");
        call.set_arg("/x");
        call.begin();
        HostError e;
        make_error(e, "limit.exceeded", "rm: big");
        CHECK(call.fail(HostStatus::Recoverable, e) == HostStatus::Internal);
    }
    {
        HostCall call(env, HostOp::Read, "open");
        call.set_arg("/x");
        // succeed() without begin(): refused, never traced at the wrong time
        CHECK(call.succeed("1") == HostStatus::Internal);
    }
    sink.lines.clear();
    {
        HostCall call(env, HostOp::Sleep, "sleep");
        call.set_arg("10");
        call.begin();
        // destroyed without an outcome
    }
    CHECK(sink.lines.size() == 1 && sink.lines[0].find("\"err\":\"internal.host_call\"") != std::string::npos);
    // a second outcome is ignored
    sink.lines.clear();
    {
        HostCall call(env, HostOp::Clock, "epoch");
        call.begin();
        CHECK(call.succeed("ok") == HostStatus::Ok);
        HostError e;
        make_error(e, "host.service_failed", "x");
        CHECK(call.fail(HostStatus::Fatal, e) == HostStatus::Ok);
    }
    CHECK(sink.lines.size() == 1);
}

void test_policy_refusal() {
    SpySink s1, s2;
    TraceChannel c1(TraceChannel::V1, &s1), c2(TraceChannel::V2, &s2);
    PathPolicy policy = script_policy();
    policy.allow = [](const std::filesystem::path& p, std::string& reason) {
        if (p.generic_string().rfind("/work/", 0) == 0) return true;
        reason = "path is outside --fs-root";
        return false;
    };
    for (TraceChannel* c : {&c1, &c2}) {
        HostCallEnv env{c, nullptr, &policy};
        HostCall call(env, HostOp::Stat, "exists");
        CHECK(call.resolve("/etc/passwd") == HostStatus::Fatal);
        CHECK(call.error().code == "runtime.invalid_operation");
        CHECK(call.error().message == "exists: path is outside --fs-root");
        CHECK(call.finished());
    }
    CHECK(s1.lines.empty());  // legacy validates before tracing
    CHECK(s2.lines.size() == 1 && s2.lines[0].find("\"err\":\"runtime.invalid_operation\"") != std::string::npos);
}

void test_recording_table() {
    SpyObserver observer;
    PathPolicy policy = script_policy();
    HostCallEnv env{nullptr, &observer, &policy};
    auto ok = [&](HostOp op, const char* raw, const Observation& obs, const char* res = "") {
        HostCall call(env, op, "b");
        if (raw) call.resolve(raw); else call.set_arg("NAME");
        call.begin();
        call.succeed(res, obs);
    };
    Observation read;
    read.bytes = "hello";
    ok(HostOp::Read, "r.txt", read);
    Observation rw;
    rw.bytes = "abc";
    rw.open_mode = "rw";
    ok(HostOp::Open, "rw.txt", rw);
    Observation r;
    r.bytes = "abc";
    r.open_mode = "r";
    ok(HostOp::Open, "r2.txt", r);
    Observation w;
    w.open_mode = "w";
    ok(HostOp::Open, "w.txt", w);
    Observation missing;
    missing.exists = false;
    ok(HostOp::Stat, "nope", missing, "missing");
    Observation listing;
    listing.listing_key = "dir:/work/dir";
    ok(HostOp::List, ".", listing);
    Observation env_value;
    env_value.env_value = std::string("v");
    ok(HostOp::Getenv, nullptr, env_value, "found");
    ok(HostOp::EnvAll, nullptr, Observation());
    Observation plat;
    plat.platform = "linux";
    ok(HostOp::Platform, nullptr, plat);
    Observation streamed;
    streamed.size = 42;
    ok(HostOp::StreamOpen, "s.txt", streamed);
    ok(HostOp::Spawn, nullptr, Observation());  // processes record nothing
    ok(HostOp::Mkdir, "d", Observation());
    const std::vector<std::string> expected = {
        "read /work/dir/r.txt 5", "read /work/dir/rw.txt 3", "read /work/dir/r2.txt 3", "presence /work/dir/nope 0 ",
        "listing dir:/work/dir", "env NAME=v", "snapshot", "platform linux", "stream_open /work/dir/s.txt", "written /work/dir/d"};
    CHECK(observer.calls == expected);
    // D-028: a failed read is recorded (absent/unreadable); failed writes,
    // output streams, w/a opens and fatal failures are not
    observer.calls.clear();
    auto fail_op = [&](HostOp op, const char* raw, const char* code, HostStatus st, const char* mode, bool output) {
        HostCall call(env, op, "b");
        call.resolve(raw);
        if (mode) call.set_open_mode(mode);
        if (output) call.set_output_stream();
        call.begin();
        HostError e;
        make_error(e, code, "b: failed");
        call.fail(st, e);
    };
    fail_op(HostOp::Read, "x", "io.open_failed", HostStatus::Recoverable, nullptr, false);
    fail_op(HostOp::Open, "o1", "io.open_failed", HostStatus::Recoverable, "rw", false);
    fail_op(HostOp::Open, "o2", "io.open_failed", HostStatus::Recoverable, "w", false);
    fail_op(HostOp::StreamOpen, "in.txt", "stream.open_failed", HostStatus::Recoverable, nullptr, false);
    fail_op(HostOp::StreamOpen, "out.txt", "stream.open_failed", HostStatus::Recoverable, nullptr, true);
    fail_op(HostOp::ReadBytes, "b.bin", "io.read_failed", HostStatus::Recoverable, nullptr, false);
    fail_op(HostOp::Save, "s", "io.write_failed", HostStatus::Recoverable, nullptr, false);
    fail_op(HostOp::Read, "big", "limit.exceeded", HostStatus::Limit, nullptr, false);
    const std::vector<std::string> failed = {"failed /work/dir/x", "failed /work/dir/o1", "failed /work/dir/in.txt", "failed /work/dir/b.bin"};
    CHECK(observer.calls == failed);
    // standalone (null observer): nothing, no crash
    HostCallEnv none{nullptr, nullptr, &policy};
    HostCall call(none, HostOp::Read, "open");
    call.resolve("x");
    call.begin();
    CHECK(call.succeed("1", read) == HostStatus::Ok);
}

void test_paths() {
    PathPolicy p = script_policy();
    Resolved r;
    HostError e;
    CHECK(resolve_path(p, "a/../b/./c", "open", r, e) == HostStatus::Ok && r.trace_path() == "/work/dir/b/c");
    CHECK(resolve_path(p, "~", "open", r, e) == HostStatus::Ok && r.trace_path() == "/home/u");
    CHECK(resolve_path(p, "~/q", "open", r, e) == HostStatus::Ok && r.trace_path() == "/home/u/q");
    CHECK(resolve_path(p, "~x", "open", r, e) == HostStatus::Ok && r.trace_path() == "/work/dir/~x");
    CHECK(resolve_path(p, "/abs/../x", "open", r, e) == HostStatus::Ok && r.trace_path() == "/x");
    CHECK(r.spelled == "/abs/../x");
    p.standalone = false;  // page: no ~ expansion, base = project root
    p.base = "/site";
    CHECK(resolve_path(p, "~/q", "open", r, e) == HostStatus::Ok && r.trace_path() == "/site/~/q");
}

OptionObject obj(std::initializer_list<std::pair<std::string, OptionValue>> items) { return OptionObject(items); }

std::string parse_err(const OptionObject& o) {
    RunOptions out;
    HostError e;
    const HostStatus s = parse_run_options(o, "run", out, e);
    if (s == HostStatus::Ok) return "OK";
    CHECK(s == HostStatus::Fatal);
    CHECK(e.code == "runtime.invalid_argument");
    CHECK(!catchable(e.code));
    return e.message;
}

void test_run_options() {
    RunOptions o;
    HostError e;
    CHECK(parse_run_options({}, "run", o, e) == HostStatus::Ok && o.present && o.mode == RunMode::Capture && !o.timeout && !o.kill_after);
    CHECK(parse_run_options(obj({{"mode", OptionValue::of_string("stream")}}), "run", o, e) == HostStatus::Ok && o.mode == RunMode::Stream);
    CHECK(parse_run_options(obj({{"mode", OptionValue::of_string("interactive")}, {"kill_after_ms", OptionValue::of_int(500)}}), "run", o, e) == HostStatus::Ok);
    CHECK(o.mode == RunMode::Interactive && o.kill_after && o.kill_after->count() == 500 && o.returns_handle());
    CHECK(parse_run_options(obj({{"mode", OptionValue::of_string("detached")}}), "run", o, e) == HostStatus::Ok && o.returns_handle());
    CHECK(parse_run_options(obj({{"timeout_ms", OptionValue::of_number(5000)}, {"kill_after_ms", OptionValue::of_number(1000)}}), "run", o, e) == HostStatus::Ok);
    CHECK(o.mode == RunMode::Capture && o.timeout->count() == 5000 && o.kill_after->count() == 1000 && !o.returns_handle());
    CHECK(parse_run_options(obj({{"timeout_ms", OptionValue::of_int(0)}}), "run", o, e) == HostStatus::Ok && o.timeout->count() == 0);
    CHECK(parse_run_options(obj({{"mode", OptionValue::of_string("capture")}}), "run", o, e) == HostStatus::Ok);
    // unknown keys: fatal with a suggestion
    CHECK(parse_err(obj({{"timout_ms", OptionValue::of_int(1)}})) == "run: unknown option 'timout_ms' (did you mean 'timeout_ms'?)");
    CHECK(parse_err(obj({{"Mode", OptionValue::of_string("stream")}})) == "run: unknown option 'Mode' (did you mean 'mode'?)");
    CHECK(parse_err(obj({{"kill-after-ms", OptionValue::of_int(1)}})) == "run: unknown option 'kill-after-ms' (did you mean 'kill_after_ms'?)");
    CHECK(parse_err(obj({{"timeuot_ms", OptionValue::of_int(1)}})) == "run: unknown option 'timeuot_ms' (did you mean 'timeout_ms'?)");
    CHECK(parse_err(obj({{"bogus", OptionValue::of_int(1)}})) == "run: unknown option 'bogus' (options: mode, timeout_ms, kill_after_ms, stdin)");
    CHECK(parse_err(obj({{"stdn", OptionValue::of_string("null")}})) == "run: unknown option 'stdn' (did you mean 'stdin'?)");
    // an unknown key is reported even when another option is also invalid
    CHECK(parse_err(obj({{"mode", OptionValue::of_int(1)}, {"tmeout_ms", OptionValue::of_int(1)}})).find("unknown option 'tmeout_ms'") != std::string::npos);
    // value errors
    CHECK(parse_err(obj({{"mode", OptionValue::of_string("streem")}})) == "run: mode must be one of capture, pipe, stream, interactive, detached (did you mean 'stream'?)");
    CHECK(parse_err(obj({{"mode", OptionValue::of_string("background")}})) == "run: mode must be one of capture, pipe, stream, interactive, detached");
    CHECK(parse_err(obj({{"mode", OptionValue::of_string("pipes")}})) == "run: mode must be one of capture, pipe, stream, interactive, detached (did you mean 'pipe'?)");
    CHECK(parse_err(obj({{"mode", OptionValue::of_int(1)}})) == "run: mode must be a string, not number");
    CHECK(parse_err(obj({{"mode", OptionValue::other("array")}})) == "run: mode must be a string, not array");
    CHECK(parse_err(obj({{"timeout_ms", OptionValue::of_int(-1)}})) == "run: timeout_ms must be a non-negative integer");
    CHECK(parse_err(obj({{"timeout_ms", OptionValue::of_number(1.5)}})) == "run: timeout_ms must be a non-negative integer");
    CHECK(parse_err(obj({{"timeout_ms", OptionValue::of_number(1e300)}})) == "run: timeout_ms must be a non-negative integer");
    CHECK(parse_err(obj({{"timeout_ms", OptionValue::of_string("5")}})) == "run: timeout_ms must be a non-negative integer");
    CHECK(parse_err(obj({{"timeout_ms", OptionValue::of_bool(true)}})) == "run: timeout_ms must be a non-negative integer");
    CHECK(parse_err(obj({{"kill_after_ms", OptionValue()}, {"mode", OptionValue::of_string("interactive")}})) == "run: kill_after_ms must be a non-negative integer");
    CHECK(parse_err(obj({{"timeout_ms", OptionValue::of_int(kMaxOptionMilliseconds + 1)}})) == "run: timeout_ms must be a non-negative integer");
    CHECK(parse_err(obj({{"timeout_ms", OptionValue::of_int(kMaxOptionMilliseconds)}})) == "OK");
    // incompatible combinations
    CHECK(parse_err(obj({{"mode", OptionValue::of_string("detached")}, {"timeout_ms", OptionValue::of_int(5)}})) == "run: deadline is not allowed for a detached process");
    CHECK(parse_err(obj({{"kill_after_ms", OptionValue::of_int(5)}})) == "run: kill_after_ms needs timeout_ms in mode capture (nothing else can cancel a blocking run)");
    CHECK(parse_err(obj({{"mode", OptionValue::of_string("stream")}, {"kill_after_ms", OptionValue::of_int(5)}})).find("in mode stream") != std::string::npos);
    CHECK(parse_err(obj({{"mode", OptionValue::of_string("detached")}, {"kill_after_ms", OptionValue::of_int(5)}})) == "OK");
    CHECK(parse_err(obj({{"mode", OptionValue::of_string("pipe")}, {"kill_after_ms", OptionValue::of_int(5)}})).find("in mode pipe") != std::string::npos);
    // mode pipe (D-034): explicit opt-in; capture keeps file capture
    CHECK(parse_run_options(obj({{"mode", OptionValue::of_string("pipe")}}), "run", o, e) == HostStatus::Ok && o.mode == RunMode::Pipe && !o.returns_handle());
    CHECK(output_mode_for(RunMode::Pipe) == OutputMode::PipeCapture);
    CHECK(output_mode_for(RunMode::Capture) == OutputMode::FileCapture);
    CHECK(output_mode_for(RunMode::Interactive) == OutputMode::Interactive);
    CHECK(parse_err(obj({{"mode", OptionValue::of_string("interactive")}, {"timeout_ms", OptionValue::of_int(5)}})) == "OK");
    // context check
    RunOptions det;
    det.mode = RunMode::Detached;
    CHECK(check_run_context(det, true, "run", e) == HostStatus::Fatal && e.code == "process.detach_in_build");
    CHECK(check_run_context(det, false, "run", e) == HostStatus::Ok);
    RunOptions cap;
    CHECK(check_run_context(cap, true, "run", e) == HostStatus::Ok);
    // edit distance
    CHECK(edit_distance("", "") == 0);
    CHECK(edit_distance("abc", "acb") == 1);
    CHECK(edit_distance("mode", "") == 4);
    CHECK(suggest_option("md", run_option_keys()).empty());  // too short to guess
    CHECK(suggest_option("timeout", run_option_keys()).empty() || suggest_option("timeout", run_option_keys()) == "timeout_ms");
}

std::string stdin_err(const OptionValue& v, std::string mode = "") {
    OptionObject o = {{"stdin", v}};
    if (!mode.empty()) o.push_back({"mode", OptionValue::of_string(mode)});
    return parse_err(o);
}

void test_stdin_option() {
    RunOptions o;
    HostError e;
    CHECK(parse_run_options({}, "run", o, e) == HostStatus::Ok && !o.stdin_source.given && o.stdin_source.kind == StdinOption::Kind::Inherit);
    auto parsed = [&](const OptionValue& v) {
        RunOptions r;
        HostError err;
        CHECK(parse_run_options(OptionObject{{"stdin", v}}, "run", r, err) == HostStatus::Ok);
        return r.stdin_source;
    };
    CHECK(parsed(OptionValue::of_string("inherit")).kind == StdinOption::Kind::Inherit);
    CHECK(parsed(OptionValue::of_string("null")).kind == StdinOption::Kind::Null);
    auto f = parsed(OptionValue::object({{"file", OptionValue::of_string("in/data.txt")}}));
    CHECK(f.kind == StdinOption::Kind::File && f.value == "in/data.txt" && f.given);
    auto t = parsed(OptionValue::object({{"text", OptionValue::of_string("a\nb\n")}}));
    CHECK(t.kind == StdinOption::Kind::Text && t.value == "a\nb\n");
    auto empty = parsed(OptionValue::object({{"text", OptionValue::of_string("")}}));
    CHECK(empty.kind == StdinOption::Kind::Text && empty.value.empty());
    // malformed forms: clear, fatal, with suggestions
    const std::string forms = "\"inherit\", \"null\", {\"file\": path} or {\"text\": string}";
    CHECK(stdin_err(OptionValue::of_string("nul")) == "run: stdin must be " + forms + " (did you mean \"null\"?)");
    CHECK(stdin_err(OptionValue::of_string("Inherit")) == "run: stdin must be " + forms + " (did you mean \"inherit\"?)");
    CHECK(stdin_err(OptionValue::of_string("input.txt")) ==
          "run: stdin must be " + forms + " (a path is given as {\"file\": \"input.txt\"}, text as {\"text\": ...})");
    CHECK(stdin_err(OptionValue::of_int(0)) == "run: stdin must be " + forms + ", not number");
    CHECK(stdin_err(OptionValue::other("array")) == "run: stdin must be " + forms + ", not array");
    CHECK(stdin_err(OptionValue::object({})) == "run: stdin object must have exactly one key, \"file\" or \"text\" (got 0)");
    CHECK(stdin_err(OptionValue::object({{"file", OptionValue::of_string("a")}, {"text", OptionValue::of_string("b")}})) ==
          "run: stdin object must have exactly one key, \"file\" or \"text\" (got 2)");
    CHECK(stdin_err(OptionValue::object({{"fiel", OptionValue::of_string("a")}})) == "run: unknown stdin form 'fiel' (did you mean 'file'?)");
    CHECK(stdin_err(OptionValue::object({{"path", OptionValue::of_string("a")}})) == "run: unknown stdin form 'path' (forms: {\"file\": path}, {\"text\": string})");
    CHECK(stdin_err(OptionValue::object({{"text", OptionValue::other("bytes")}})) ==
          "run: stdin.text must be a string, not bytes (binary stdin is not supported yet; text is never reinterpreted from bytes)");
    CHECK(stdin_err(OptionValue::object({{"text", OptionValue::of_int(3)}})) == "run: stdin.text must be a string, not number");
    CHECK(stdin_err(OptionValue::object({{"file", OptionValue()}})) == "run: stdin.file must be a string, not null");
    CHECK(stdin_err(OptionValue::object({{"file", OptionValue::of_string("")}})) == "run: stdin.file must not be empty");
    // combinations
    CHECK(stdin_err(OptionValue::of_string("null"), "interactive") == "run: stdin is not allowed in mode interactive (the process reads what write() sends)");
    CHECK(stdin_err(OptionValue::of_string("inherit"), "detached") == "run: stdin \"inherit\" is not allowed for a detached process (use \"null\" or {\"file\": path})");
    CHECK(stdin_err(OptionValue::object({{"text", OptionValue::of_string("x")}}), "detached").find("{\"text\": ...} is not allowed for a detached process") != std::string::npos);
    CHECK(stdin_err(OptionValue::of_string("null"), "detached") == "OK");
    CHECK(stdin_err(OptionValue::object({{"file", OptionValue::of_string("f")}}), "detached") == "OK");
    CHECK(stdin_err(OptionValue::object({{"text", OptionValue::of_string("x")}}), "pipe") == "OK");
    CHECK(stdin_err(OptionValue::of_string("null"), "stream") == "OK");
    CHECK(parse_err(obj({{"stdin", OptionValue::of_string("null")}, {"stdin", OptionValue::of_string("null")}})) == "run: option 'stdin' given twice");
}

void test_stdin_file_and_request() {
    char dir[] = "/tmp/cp16-stdin-XXXXXX";
    CHECK(::mkdtemp(dir) != nullptr);
    const std::string d = dir;
    std::filesystem::create_directories(d + "/real/sub");
    { std::ofstream f(d + "/real/data.txt", std::ios::binary); f << "hello\n"; }
    { std::ofstream f(d + "/big.txt", std::ios::binary); f << std::string(5000, 'x'); }
    std::filesystem::create_directory_symlink(d + "/real/sub", d + "/link");
    SpySink sink;
    SpyObserver observer;
    TraceChannel channel(TraceChannel::V1, &sink);
    PathPolicy policy;
    policy.standalone = true;
    policy.base = d;
    HostCallEnv env{&channel, &observer, &policy};
    RunRequestContext ctx;
    ctx.read_limit = 4096;
    std::string bytes;
    HostError e;
    // normal OS semantics: link/../data.txt is real/data.txt (lexically it would be <dir>/data.txt, which does not exist)
    CHECK(read_stdin_file(env, "link/../data.txt", ctx, bytes, e) == HostStatus::Ok && bytes == "hello\n");
    const std::string canon = std::filesystem::weakly_canonical(d + "/real/data.txt").generic_string();
    CHECK(observer.calls.size() == 1 && observer.calls[0] == "read " + canon + " 6");  // dependency recorded
    CHECK(sink.lines.size() == 1 && sink.lines[0] == "{\"ctx\":\"script\",\"op\":\"read\",\"arg\":\"" + canon + "\",\"res\":\"6\"}\n");
    observer.calls.clear();
    // missing / directory: catchable io.open_failed, recorded as failed reads (D-028)
    CHECK(read_stdin_file(env, "nope.txt", ctx, bytes, e) == HostStatus::Recoverable && e.code == "io.open_failed" && catchable(e.code));
    CHECK(e.message == "run: cannot open stdin file nope.txt");
    CHECK(read_stdin_file(env, "real", ctx, bytes, e) == HostStatus::Recoverable && e.message == "run: cannot open stdin file real: it is a directory");
    // read limit: loud, never truncated
    CHECK(read_stdin_file(env, "big.txt", ctx, bytes, e) == HostStatus::Limit && e.code == "limit.exceeded" && bytes.empty());
    CHECK(e.message == "run: stdin file big.txt exceeds the read limit of 4096 bytes (NIFT_READ_LIMIT)");
    CHECK(observer.calls.size() == 2 && observer.calls[0] == "failed " + d + "/nope.txt" && observer.calls[1].rfind("failed ", 0) == 0);
    // --fs-root policy (checked on the canonical path)
    PathPolicy rooted = policy;
    rooted.allow = [&](const std::filesystem::path& p, std::string& reason) {
        if (p.generic_string().rfind(std::filesystem::weakly_canonical(d + "/real/sub").generic_string(), 0) == 0) return true;
        reason = "path is outside --fs-root";
        return false;
    };
    HostCallEnv renv{&channel, &observer, &rooted};
    CHECK(read_stdin_file(renv, "link/../data.txt", ctx, bytes, e) == HostStatus::Fatal && e.message == "run: path is outside --fs-root");
    // request building
    RunOptions opts;
    SpawnRequest req;
    CHECK(parse_run_options(OptionObject{{"mode", OptionValue::of_string("pipe")}, {"timeout_ms", OptionValue::of_int(50)},
                                         {"stdin", OptionValue::object({{"file", OptionValue::of_string("real/data.txt")}})}},
                            "run", opts, e) == HostStatus::Ok);
    CHECK(build_spawn_request(env, opts, {"cat", "-"}, ctx, req, e) == HostStatus::Ok);
    CHECK(req.mode == OutputMode::PipeCapture && req.stdin_mode == StdinMode::Text && req.stdin_text == "hello\n");
    CHECK(req.timeout && req.timeout->count() == 50 && req.kill_after == kDefaultKillAfter && req.capture_limit == 4096);
    CHECK(req.stages.size() == 1 && req.stages[0].program == "cat" && req.stages[0].args == std::vector<std::string>{"-"});
    RunOptions plain;
    CHECK(build_spawn_request(env, plain, {"true"}, ctx, req, e) == HostStatus::Ok && req.mode == OutputMode::FileCapture && req.stdin_mode == StdinMode::Inherit);
    RunOptions det;
    det.mode = RunMode::Detached;
    RunRequestContext bctx = ctx;
    bctx.in_build = true;
    CHECK(build_spawn_request(env, det, {"sleep", "1"}, bctx, req, e) == HostStatus::Fatal && e.code == "process.detach_in_build");
    std::filesystem::remove_all(d);
}

void test_shutdown_refusals() {
    SpySink sink;
    TraceChannel channel(TraceChannel::V2, &sink);
    PathPolicy policy = script_policy();
    HostCallEnv env{&channel, nullptr, &policy};
    runtime::request_shutdown_for_test(SIGTERM);
    {
        HostCall call(env, HostOp::Save, "save");
        call.resolve("x");
        CHECK(call.begin() == HostStatus::Fatal && call.error().code == "host.shutting_down" && call.finished());
    }
    {
        HostCall call(env, HostOp::Spawn, "run");
        call.set_arg("true");
        CHECK(call.begin() == HostStatus::Fatal);
    }
    {
        HostCall call(env, HostOp::Read, "open");  // reads stay possible: the shutdown path may need them
        call.resolve("x");
        CHECK(call.begin() == HostStatus::Ok);
        call.succeed("1");
    }
    SpawnRequest r;
    SpawnStage st;
    st.program = "true";
    r.stages.push_back(st);
    HostError e;
    CHECK(ProcessGroup::validate(r, e) == HostStatus::Fatal && e.code == "host.shutting_down");
    runtime::clear_shutdown_for_test();
    CHECK(ProcessGroup::validate(r, e) == HostStatus::Ok);
    CHECK(sink.lines.size() == 3 && sink.lines[0].find("\"err\":\"host.shutting_down\"") != std::string::npos);
    CHECK(!catchable("host.shutting_down"));
}

} // namespace

// Legacy routing (cp16-host-ops-impl.md §9): adopted targets, file().open's
// open_read hook, listings recorded whatever the outcome.
void test_legacy_routing_hooks() {
    SpyObserver observer;
    HostCallEnv env{nullptr, &observer, nullptr};
    {
        HostCall call(env, HostOp::Read, "open");
        call.adopt("/p/../q/./r.txt");
        CHECK(call.target().trace_path() == "/q/r.txt");          // trace: absolute, lexically normal
        CHECK(call.target().path.generic_string() == "/p/../q/./r.txt");  // recording: as given
        call.begin();
        Observation o;
        o.bytes = "xy";
        call.succeed("2", o);
    }
    struct OpenSpy : SpyObserver {
        void open_read(const std::filesystem::path& p, std::string_view bytes) override {
            calls.push_back("open_read " + p.generic_string() + " " + std::to_string(bytes.size()));
        }
    } open_spy;
    HostCallEnv open_env{nullptr, &open_spy, nullptr};
    for (const char* mode : {"r", "rw", "w", "a"}) {
        HostCall call(open_env, HostOp::Open, "open");
        call.adopt(std::string("/o/") + mode);
        call.set_open_mode(mode);
        call.begin();
        Observation o;
        o.bytes = "abc";
        call.succeed("", o);
    }
    CHECK((open_spy.calls == std::vector<std::string>{"open_read /o/r 3", "open_read /o/rw 3"}));
    // the default open_read is read()
    {
        HostCall call(env, HostOp::Open, "open");
        call.adopt("/o/default");
        call.set_open_mode("r");
        call.begin();
        Observation o;
        o.bytes = "abcd";
        call.succeed("", o);
    }
    // listing: recorded on success, on a recoverable and on a fatal failure;
    // a refusal (shutdown, policy) records nothing
    auto list = [&](const char* key, HostStatus st, const char* code) {
        HostCall call(env, HostOp::List, "ls");
        call.adopt("/d");
        call.set_listing_key(key);
        call.begin();
        if (st == HostStatus::Ok) { call.succeed(); return; }
        HostError e;
        make_error(e, code, "ls: failed");
        call.fail(st, e);
    };
    list("dir:a", HostStatus::Ok, nullptr);
    list("dir:b", HostStatus::Recoverable, "io.directory_read_failed");
    list("dir:c", HostStatus::Fatal, "runtime.invalid_operation");
    const std::vector<std::string> expected = {"read /p/../q/./r.txt 2", "read /o/default 4", "listing dir:a", "listing dir:b",
                                               "listing dir:c"};
    CHECK(observer.calls == expected);
}

int main() {
    test_error_table();
    test_op_table();
    test_trace_format();
    test_hostcall_points();
    test_hostcall_classification();
    test_policy_refusal();
    test_recording_table();
    test_paths();
    test_run_options();
    test_stdin_option();
    test_stdin_file_and_request();
    test_shutdown_refusals();
    test_legacy_routing_hooks();
    return rt_test::finish("host-layer-unit");
}
