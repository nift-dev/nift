// Runtime 2.0 CP02: dynamic reads that can change rendered bytes are recorded
// on the page's RenderResult so the incremental build engine can re-check
// them. Standalone scripts record nothing: there is no page to invalidate.
#include "Parser.h"
#include "ReadObservation.h"
#include "HostTrace.h"
#include "ReadLimit.h"
#include "ParserAsyncPool.h"
#include "ParserHost.h"

#include <fstream>

namespace fs = std::filesystem;
namespace host = nift::rt::host;

namespace {
// Trace spelling: absolute, lexically normal (the harness maps temp roots).
std::string trace_path(const fs::path& path) {
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    return (error ? path : absolute).lexically_normal().generic_string();
}
} // namespace

void Parser::trace_host(const char* op, const fs::path& path, const std::string& result) const {
    if (nift::trace::enabled()) nift::trace::emit(op, trace_path(path), result);
}

// CP16: the observation primitives run their operation as a HostCall (trace
// from the op table, recording through ParserHostObserver). Ops traced after
// the attempt (stat, read, getenv) are equivalent whether the primitive is
// called around the attempt or right after it.
Parser::FsInfo Parser::inspect_path(const fs::path& path) const {
    ParserHostObserver observer(*this);
    host::HostCall call(parser_host_env(&observer), host::HostOp::Stat, "stat");
    call.adopt(path);
    call.begin();
    FsInfo info = inspect_path_unobserved(path);
    if (info.error) {
        call.fail(host::HostStatus::Recoverable, parser_host_error("io.metadata_failed", info.error_message), "error");
        return info;
    }
    host::Observation observation;
    observation.exists = info.exists;
    observation.kind = info.type;
    call.succeed(info.exists ? info.type : std::string("missing"), observation);
    return info;
}

// Recording-only helpers: the one implementation is PageObserver (shared by
// every engine); these keep the legacy call sites that record without a Host
// call (page members, copy sources, template absences).
void Parser::observe_presence(const fs::path& path, bool exists, const std::string& kind) const {
    ParserHostObserver(*this).presence(path, exists, kind);
}

void Parser::observe_read(const fs::path& path, const std::string& bytes) {
    ParserHostObserver observer(*this);
    host::HostCall call(parser_host_env(&observer), host::HostOp::Read, "read");
    call.adopt(path);
    call.begin();
    host::Observation observation;
    observation.bytes = bytes;
    call.succeed(std::to_string(bytes.size()), observation);
}

void Parser::observe_streamed_read(const fs::path& path, std::uint64_t size, const std::uint64_t* snapshot) {
    ParserHostObserver observer(*this);
    host::HostCall call(parser_host_env(&observer), host::HostOp::ReadBytes, "read");
    call.adopt(path);
    call.begin();
    host::Observation observation;
    observation.size = size;
    observation.hash = snapshot;
    call.succeed(std::to_string(size), observation);
}

void Parser::observe_failed_read(const fs::path& path) { ParserHostObserver(*this).failed_read(path); }

void Parser::observe_stream_open(const fs::path& path) { ParserHostObserver(*this).stream_open(path); }

void Parser::observe_page_member(const std::string& page, const std::string& member) {
    if (member != "content" && member != "metadata") return;
    if (!observing()) return;
    for (const auto& source : host_.page_member_sources(page, member)) {
        std::error_code error;
        if (fs::exists(source, error) && !error) observe_stream_open(source);
        else observe_failed_read(source);
    }
}

void Parser::observe_copy_source(const fs::path& source) {
    if (!observing()) return;
    std::error_code error;
    if (fs::exists(source, error) && !error) observe_stream_open(source);
    else observe_failed_read(source);
}

bool Parser::guard_build_write(const std::string& operation, const fs::path& path, std::string& error, bool removes) {
    if (!observing()) return true;
    // Resolve with filesystem semantics so `..`, symlinked directories and
    // alternative spellings name the same file: the canonical parent plus the
    // final name (what an unlink/rename replaces), and, for writes that
    // follow a final symlink (copy_file, ofstream, touch, save's target
    // lookup), the fully resolved path too. Checked immediately before the
    // operation; a change between the check and the write is not prevented
    // (documented race, no transactional redesign).
    std::error_code ignored;
    const fs::path absolute = fs::absolute(path, ignored).lexically_normal();
    std::vector<fs::path> candidates;
    const fs::path parent = fs::weakly_canonical(absolute.parent_path(), ignored);
    candidates.push_back(ignored ? absolute : (parent / absolute.filename()).lexically_normal());
    if (!removes) {
        std::error_code resolve_error;
        const fs::path resolved = fs::weakly_canonical(absolute, resolve_error);
        if (!resolve_error && resolved != candidates.front()) candidates.push_back(resolved);
    }
    std::error_code root_error;
    const fs::path metadata = fs::weakly_canonical(host_.root() / ".nift", root_error);
    for (const auto& candidate : candidates) {
        if (filesystem::path_within(root_error ? (host_.root() / ".nift").lexically_normal() : metadata, candidate)) {
            active_recoverable_.reset();
            return fail_fatal(nift::detail::DiagnosticCode::PolicyRestrictedOperation,
                              operation + ": refusing to write Nift build metadata during a build: " + path.generic_string(), error);
        }
        if (const auto owner = host_.output_owner(candidate); owner && *owner != tracked_info_.name) {
            active_recoverable_.reset();
            return fail_fatal(nift::detail::DiagnosticCode::PolicyRestrictedOperation,
                              operation + ": refusing to " + (removes ? "remove" : "overwrite") + " the build output of tracked page '" +
                                  *owner + "' during a build: " + path.generic_string(), error);
        }
    }
    return true;
}

bool Parser::read_limit(const std::string& operation, std::uint64_t& limit, std::string& error) {
    const auto setting = nift::readlimit::current();
    if (!setting.valid) {
        active_recoverable_.reset();
        return fail_fatal(nift::detail::DiagnosticCode::NativeInvalidArgument,
                          nift::readlimit::invalid_message(operation, setting), error);
    }
    limit = setting.bytes;
    return true;
}

bool Parser::fail_read_limit(const std::string& operation, const std::string& what, std::uint64_t limit, std::string& error) {
    // Never an ordinary (catchable) error: drop any pending recoverable value.
    active_recoverable_.reset();
    return fail_fatal(nift::detail::DiagnosticCode::NativeLimitExceeded,
                      nift::readlimit::exceeded_message(operation, what, limit), error);
}

namespace {
template <class Buffer>
ParserWholeRead read_whole_impl(const fs::path& path, Buffer& out, std::uint64_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::error_code status_error;
        const auto status = fs::status(path, status_error);
        const bool missing = (!status_error && !fs::exists(status)) ||
                             status_error == std::errc::no_such_file_or_directory ||
                             status_error == std::errc::not_a_directory;
        return missing ? ParserWholeRead::Missing : ParserWholeRead::OpenFailed;
    }
    // st_size only pre-sizes the buffer and rejects a regular file that is
    // already too large without reading it; the bound itself is enforced
    // while reading (FIFOs and devices report 0, files can grow).
    std::uint64_t hint = 0;
    std::error_code size_error;
    if (fs::is_regular_file(path, size_error)) {
        const auto size = fs::file_size(path, size_error);
        if (!size_error) {
            if (size > limit) return ParserWholeRead::Limit;
            hint = size;
        }
    }
    Buffer data;
    switch (nift::readlimit::read_bounded(in, data, limit, std::numeric_limits<std::uint64_t>::max(), hint)) {
        case nift::readlimit::Status::Ok: out = std::move(data); return ParserWholeRead::Ok;
        case nift::readlimit::Status::Exceeded: return ParserWholeRead::Limit;
        case nift::readlimit::Status::ReadError: break;
    }
    return ParserWholeRead::ReadFailed;
}
} // namespace

Parser::WholeRead Parser::read_whole_file(const std::string& operation, const fs::path& path, std::string& out,
                                          std::string& error) {
    std::uint64_t limit = 0;
    if (!read_limit(operation, limit, error)) return WholeRead::Limit;
    const auto outcome = read_whole_impl(path, out, limit);
    if (outcome == WholeRead::Limit) fail_read_limit(operation, path.generic_string(), limit, error);
    return outcome;
}

Parser::WholeRead Parser::read_whole_file(const std::string& operation, const fs::path& path, nift::RuntimeBytes& out,
                                          std::string& error) {
    std::uint64_t limit = 0;
    if (!read_limit(operation, limit, error)) return WholeRead::Limit;
    const auto outcome = read_whole_impl(path, out, limit);
    if (outcome == WholeRead::Limit) fail_read_limit(operation, path.generic_string(), limit, error);
    return outcome;
}

bool Parser::read_snapshot_wanted() const {
    return observing() && host_.observe_dependencies;
}

void Parser::observe_metadata_read(const fs::path& path) { ParserHostObserver(*this).metadata_read(path); }

void Parser::note_written(const fs::path& path, const char* op) {
    if (nift::trace::enabled()) trace_host(op, path, "");
    note_written_untraced(path);
}

void Parser::note_written_untraced(const fs::path& path) { ParserHostObserver(*this).written(path); }

// A listing observed before the attempt (legacy order): the fingerprint is
// computed from the filesystem itself, whatever the attempt then finds.
void Parser::observe_listing(const std::string& key, const fs::path& path) {
    ParserHostObserver observer(*this);
    host::HostCall call(parser_host_env(&observer), key.rfind("glob:", 0) == 0 ? host::HostOp::Glob : host::HostOp::List, "ls");
    call.adopt(path);
    call.set_listing_key(key);
    call.begin();
    call.succeed();
}

void Parser::observe_environment(const std::string& name, const nift::HostResult& value, bool template_directive) {
    ParserHostObserver observer(*this);
    host::HostCall call(parser_host_env(&observer), template_directive ? host::HostOp::GetenvTemplate : host::HostOp::Getenv,
                        "getenv");
    call.set_arg(name);
    call.begin();
    if (value.status == nift::HostStatus::Error) {
        call.fail(host::HostStatus::Fatal, parser_host_error("host.service_failed", "getenv: " + value.error), "error");
        return;
    }
    host::Observation observation;
    if (value.status == nift::HostStatus::Found) observation.env_value = value.value;
    call.succeed(value.status == nift::HostStatus::Found ? "found" : "unset", observation);
}

std::string RenderHost::listing_fingerprint(const std::string& key) const {
    return nift::observe::listing_fingerprint(root(), key);
}

void Parser::settle_worker_observations(RenderResult& into) {
    auto merge = [&](const std::shared_ptr<WorkerObservations>& observed) {
        if (!observed) return;
        into.dependencies.insert(observed->dependencies.begin(), observed->dependencies.end());
        into.reqs.insert(observed->reqs.begin(), observed->reqs.end());
        into.observed.merge(observed->observed);
    };
    for (auto& st : owned_async_instances_) {
        std::unique_lock<std::mutex> lk(st->mutex);
        while (st->completion.pending()) {
            lk.unlock();
            if (nift::detail::NiftAsyncPool::is_worker_thread() && nift::detail::NiftAsyncPool::instance().run_one()) { lk.lock(); continue; }
            lk.lock();
            st->cv.wait_for(lk, std::chrono::milliseconds(1), [&] { return !st->completion.pending(); });
        }
        if (!st->observed_merged) { merge(st->observed); st->observed_merged = true; }
    }
    for (auto& st : owned_thread_instances_) {
        { std::lock_guard<std::mutex> guard(st->join_mutex); if (st->worker.joinable()) st->worker.join(); }
        std::lock_guard<std::mutex> lk(st->mutex);
        if (!st->observed_merged) { merge(st->observed); st->observed_merged = true; }
    }
}

std::shared_ptr<Parser::WorkerObservations> Parser::export_worker_observations() {
    // Nested workers first: their reads are this worker's reads.
    settle_worker_observations(result_);
    auto observed = std::make_shared<WorkerObservations>();
    observed->dependencies = result_.dependencies;
    observed->reqs = result_.reqs;
    observed->observed = result_.observed;
    return observed;
}
