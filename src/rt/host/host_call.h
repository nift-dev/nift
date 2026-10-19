#pragma once
// CP16 Host layer: HostCall, the single choke point every Host op goes
// through (design §2): resolve -> trace (v1 point from the op table) ->
// attempt (by the caller) -> classify -> trace (v2, after the attempt) ->
// record (observer, success only).
//
// Usage (a native):
//
//   HostCall call(env, HostOp::Read, "open");
//   if (call.resolve(raw) != HostStatus::Ok) return call.status();   // already traced/reported
//   if (call.begin() != HostStatus::Ok) return call.status();       // v1 Pre point
//   std::string data; HostError e;
//   if (!read_it(call.target().path, data, e)) return call.fail(HostStatus::Recoverable, e);
//   call.succeed(std::to_string(data.size()), obs);                 // v1 PostOk point, v2, record
//
// Every call must end with exactly one succeed()/fail(); a HostCall destroyed
// without one (an exception or an early return the native forgot) reports
// `internal.host_call` on the v2 channel. fail() checks the reported status
// against the code table (host_error.h): a mismatch becomes Internal, so a
// native cannot make a catchable error fatal or the reverse.
#include "rt/host/host_error.h"
#include "rt/host/host_op.h"
#include "rt/host/host_trace.h"
#include "rt/host/observer.h"
#include "rt/host/path.h"

#include <string>
#include <string_view>
#include <vector>

namespace nift::rt::host {

struct HostCallEnv {
    TraceChannel* trace = nullptr;  // null: no tracing
    Observer* observer = nullptr;   // null: standalone script (no recording)
    const PathPolicy* paths = nullptr;
};

class HostCall {
public:
    HostCall(const HostCallEnv& env, HostOp op, std::string_view builtin);
    ~HostCall();
    HostCall(const HostCall&) = delete;
    HostCall& operator=(const HostCall&) = delete;

    // Path ops: resolve once; trace, the op and recording all use the result.
    // A refusal by the path policy finishes the call (v2 line, no v1 line:
    // legacy validates before tracing).
    HostStatus resolve(std::string_view raw, PathSemantics semantics = PathSemantics::Lexical);
    const Resolved& target() const { return target_; }
    // Non-path ops: the trace `arg` (variable name, program, ...).
    void set_arg(std::string arg) { arg_ = std::move(arg); has_arg_ = true; }
    // spawn: v1 `arg` is program and args joined by spaces, `res` the cwd as
    // spelled (legacy); v2 gets the argv array and the absolute cwd.
    void set_spawn(const std::vector<std::string>& argv, const std::string& cwd_spelled, const std::string& cwd_absolute);
    // v1 result text for ops whose Pre line carries one (open: the mode; spawn: cwd).
    void set_pre_result(std::string res) { pre_res_ = std::move(res); }

    // Writes the v1 line for Pre ops. While Nift is shutting down (SIGINT/
    // SIGTERM received) an externally visible op (writes, spawns) is refused
    // here with `host.shutting_down` and the call is finished.
    HostStatus begin();
    HostStatus succeed(std::string res = std::string(), const Observation& obs = Observation());
    HostStatus fail(HostStatus status, const HostError& error, std::string v1_res = "error");

    HostOp op() const { return op_; }
    const std::string& builtin() const { return builtin_; }
    HostStatus status() const { return status_; }
    const HostError& error() const { return error_; }
    bool finished() const { return finished_; }

private:
    std::string trace_arg() const;
    void emit_v2(const std::string& res, const std::string& err);
    void record(const Observation& obs);

    HostCallEnv env_;
    HostOp op_;
    std::string builtin_;
    Resolved target_;
    bool resolved_ = false;
    std::string arg_;
    bool has_arg_ = false;
    std::string pre_res_;
    bool spawn_ = false;
    std::vector<std::string> argv_;
    std::string cwd_absolute_;
    bool begun_ = false;
    bool refused_ = false;
    bool finished_ = false;
    HostStatus status_ = HostStatus::Ok;
    HostError error_;
};

} // namespace nift::rt::host
