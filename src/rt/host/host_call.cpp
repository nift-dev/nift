#include "rt/host/host_call.h"
// rt-lint: cold-file one Host op per call (I/O-bound): mode names compared per open, never on the VM dispatch path

#include "rt/host/process_runtime.h"

namespace nift::rt::host {

HostCall::HostCall(const HostCallEnv& env, HostOp op, std::string_view builtin)
    : env_(env), op_(op), builtin_(builtin) {}

HostCall::~HostCall() {
    if (finished_) return;
    HostError e;
    make_error(e, "internal.host_call", builtin_ + ": host call ended without an outcome");
    status_ = HostStatus::Internal;
    error_ = e;
    finished_ = true;
    emit_v2("error", e.code);
}

HostStatus HostCall::resolve(std::string_view raw, PathSemantics semantics) {
    static const PathPolicy default_policy;
    const PathPolicy& policy = env_.paths ? *env_.paths : default_policy;
    HostError e;
    const HostStatus s = resolve_path(policy, raw, builtin_, target_, e, semantics);
    if (s != HostStatus::Ok) {
        target_.path = std::filesystem::path(std::string(raw));
        resolved_ = true;
        begun_ = true;
        refused_ = true;  // before the attempt: no v1 line (legacy validates first), v2 reports it
        return fail(s, e);
    }
    resolved_ = true;
    return HostStatus::Ok;
}

void HostCall::adopt(const std::filesystem::path& resolved) {
    target_ = Resolved();
    target_.spelled = resolved.generic_string();
    target_.path = resolved;
    resolved_ = true;
}

void HostCall::set_spawn(const std::vector<std::string>& argv, const std::string& cwd_spelled,
                         const std::string& cwd_absolute) {
    spawn_ = true;
    argv_ = argv;
    std::string joined;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i) joined.push_back(' ');
        joined += argv[i];
    }
    arg_ = joined;
    has_arg_ = true;
    pre_res_ = cwd_spelled;
    cwd_absolute_ = cwd_absolute;
}

std::string HostCall::trace_arg() const {
    if (has_arg_) return arg_;
    if (resolved_) return target_.trace_path();
    return std::string();
}

HostStatus HostCall::begin() {
    if (begun_) return finished_ ? status_ : HostStatus::Ok;
    begun_ = true;
    if ((op_is_write(op_) || op_ == HostOp::Spawn) && runtime::shutting_down()) {
        refused_ = true;
        HostError e;
        make_error(e, "host.shutting_down",
                   builtin_ + ": Nift is shutting down (signal received); nothing new is written or started");
        return fail(HostStatus::Fatal, e);
    }
    const auto& info = op_info(op_);
    if (env_.trace && info.v1_point == TracePoint::Pre) env_.trace->emit_v1(info.v1_name, trace_arg(), pre_res_);
    return HostStatus::Ok;
}

void HostCall::emit_v2(const std::string& res, const std::string& err) {
    if (!env_.trace || !env_.trace->v2()) return;
    TraceV2Record r;
    r.op = op_v2_name(op_);
    r.arg = spawn_ && !argv_.empty() ? argv_.front() : trace_arg();
    r.res = res;
    r.err = err;
    if (spawn_) {
        r.has_argv = true;
        r.argv = argv_;
        r.has_cwd = true;
        r.cwd = cwd_absolute_;
    }
    env_.trace->emit_v2(r);
}

HostStatus HostCall::succeed(std::string res, const Observation& obs) {
    if (finished_) return status_;
    if (!begun_) {
        // A native that skipped begin() would have missed a Pre line: refuse
        // rather than trace at the wrong time.
        HostError e;
        make_error(e, "internal.host_call", builtin_ + ": host call finished before it began");
        return fail(HostStatus::Internal, e);
    }
    finished_ = true;
    status_ = HostStatus::Ok;
    const auto& info = op_info(op_);
    if (env_.trace && (info.v1_point == TracePoint::PostOk || info.v1_point == TracePoint::PostAlways))
        env_.trace->emit_v1(info.v1_name, trace_arg(), res);
    emit_v2(res.empty() ? std::string("ok") : res, std::string());
    record(obs);
    return HostStatus::Ok;
}

HostStatus HostCall::fail(HostStatus status, const HostError& error, std::string v1_res) {
    if (finished_) return status_;
    finished_ = true;
    error_ = error;
    status_ = status;
    const HostStatus expected = status_for_code(error.code);
    if (status == HostStatus::Ok || expected != status) {
        // Classification bug in the caller: never let it through as reported.
        make_error(error_, "internal.host_call",
                   builtin_ + ": host error '" + error.code + "' reported as " + status_name(status));
        status_ = HostStatus::Internal;
    }
    const auto& info = op_info(op_);
    if (env_.trace && begun_ && !refused_ && info.v1_point == TracePoint::PostAlways)
        env_.trace->emit_v1(info.v1_name, trace_arg(), v1_res);
    emit_v2("error", error_.code);
    // Fatal/limit: the run ends and legacy records no read. A listing is the
    // exception: legacy records its fingerprint before the attempt, whatever follows.
    if (begun_ && !refused_ &&
        (status_ == HostStatus::Recoverable || (status_ == HostStatus::Fatal && info.recording == Recording::Listing)))
        record_failure();
    return status_;
}

void HostCall::record(const Observation& obs) {
    Observer* o = env_.observer;
    if (!o) return;
    const auto& path = target_.path;
    switch (op_info(op_).recording) {
        case Recording::None: break;
        case Recording::Read: o->read(path, obs.bytes); break;
        case Recording::StreamedRead: o->streamed_read(path, obs.size, obs.hash); break;
        case Recording::StreamOpen: if (!output_stream_) o->stream_open(path); break;
        case Recording::Presence:
            o->presence(path, obs.exists, obs.kind);
            if (obs.metadata && obs.exists && obs.kind == "file") o->metadata_read(path);
            break;
        case Recording::Listing: o->listing(obs.listing_key.empty() ? listing_key_ : obs.listing_key, path); break;
        case Recording::Environment: o->environment(arg_, obs.env_value); break;
        case Recording::EnvironmentSnapshot: o->environment_snapshot(); break;
        case Recording::Platform: o->platform(obs.platform); break;
        case Recording::Written: o->written(path); break;
        case Recording::OpenMode: {
            const std::string_view mode = obs.open_mode.empty() ? std::string_view(open_mode_) : obs.open_mode;
            if (mode == "r" || mode == "rw") o->open_read(path, obs.bytes);  // "rw" reads the file too (D-028)
            break;
        }
    }
}

// D-028: a read that failed is recorded too (absent when missing, unreadable
// otherwise -- the observer decides which), so the page is rebuilt when the
// path appears or becomes readable. Only read-class ops; a refusal by the
// path policy is not a read attempt.
void HostCall::record_failure() {
    Observer* o = env_.observer;
    if (!o || !resolved_) return;
    bool reading = false;
    switch (op_info(op_).recording) {
        case Recording::Read: case Recording::StreamedRead: reading = true; break;
        case Recording::StreamOpen: reading = !output_stream_; break;
        case Recording::OpenMode: reading = open_mode_ == "r" || open_mode_ == "rw"; break;
        case Recording::Listing:
            // The fingerprint says "missing"/"unreadable": the page is rebuilt
            // when the directory appears or becomes readable.
            if (!listing_key_.empty()) o->listing(listing_key_, target_.path);
            return;
        default: break;
    }
    if (reading) o->failed_read(target_.path);
}

} // namespace nift::rt::host
