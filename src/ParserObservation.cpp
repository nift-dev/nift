// Runtime 2.0 CP02: dynamic reads that can change rendered bytes are recorded
// on the page's RenderResult so the incremental build engine can re-check
// them. Standalone scripts record nothing: there is no page to invalidate.
#include "Parser.h"
#include "ReadObservation.h"
#include "HostTrace.h"
#include "ReadLimit.h"
#include "ParserAsyncPool.h"

#include <fstream>

namespace fs = std::filesystem;

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

Parser::FsInfo Parser::inspect_path(const fs::path& path) const {
    FsInfo info = inspect_path_unobserved(path);
    if (nift::trace::enabled()) trace_host("stat", path, info.error ? "error" : info.exists ? info.type : "missing");
    if (!info.error) observe_presence(path, info.exists, info.type);
    return info;
}

void Parser::observe_presence(const fs::path& path, bool exists, const std::string& kind) const {
    if (!observing()) return;
    // inspect_path is const for its callers, but the Parser itself is never a
    // const object while rendering; recording an observation is not a change
    // to the evaluation state that const protects.
    auto& result = const_cast<Parser*>(this)->result_;
    const std::string spelling = nift::observe::spell(host_.root(), path);
    if (exists) {
        result.reqs.insert(spelling);                // stale if it disappears
        if (!kind.empty()) result.observed.kinds[spelling] = kind;  // stale if its type changes
    } else {
        result.observed.absent.insert(spelling);     // stale if it appears
    }
}

void Parser::observe_read(const fs::path& path, const std::string& bytes) {
    if (nift::trace::enabled()) trace_host("read", path, std::to_string(bytes.size()));
    if (!observing()) return;
    result_.dependencies.insert(host_.relative(path));
    host_.observe_dependency(path, bytes);
}

void Parser::observe_streamed_read(const fs::path& path, std::uint64_t size, const std::uint64_t* snapshot) {
    if (nift::trace::enabled()) trace_host("read", path, std::to_string(size));
    if (!observing()) return;
    result_.dependencies.insert(host_.relative(path));
    if (snapshot) host_.observe_dependency_hash(path, *snapshot);
}

void Parser::observe_failed_read(const fs::path& path) {
    if (!observing()) return;
    std::error_code error;
    const auto status = fs::status(path, error);
    if (!error && !fs::exists(status)) { observe_presence(path, false); return; }
    if (error && (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)) {
        observe_presence(path, false);
        return;
    }
    result_.observed.unreadable[nift::observe::spell(host_.root(), path)] = filesystem::read_access_error(path);
}

void Parser::observe_stream_open(const fs::path& path) {
    if (!observing()) return;
    result_.dependencies.insert(host_.relative(path));
    // Hashed at open: an edit made while the page streams the file leaves the
    // recorded snapshot different from the file, so the next build rebuilds.
    if (read_snapshot_wanted()) host_.observe_dependency_hash(path, filesystem::hash_path_streamed(path));
}

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

void Parser::observe_metadata_read(const fs::path& path) {
    if (!observing()) return;
    result_.dependencies.insert(host_.relative(path));
    host_.observe_dependency_hash(path, filesystem::hash_path(path));
}

void Parser::note_written(const fs::path& path, const char* op) {
    if (nift::trace::enabled()) trace_host(op, path, "");
    note_written_untraced(path);
}

void Parser::note_written_untraced(const fs::path& path) {
    if (!observing()) return;
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    result_.observed.written.insert((error ? path : absolute).lexically_normal());
}

void Parser::observe_listing(const std::string& key, const fs::path& path) {
    if (nift::trace::enabled()) trace_host(key.rfind("glob:", 0) == 0 ? "glob" : "list", path, "");
    if (!observing()) return;
    result_.observed.listings[key] = host_.listing_fingerprint(key);
}

void Parser::observe_environment(const std::string& name, const nift::HostResult& value) {
    if (nift::trace::enabled())
        nift::trace::emit("getenv", name, value.status == nift::HostStatus::Found ? "found" :
                                          value.status == nift::HostStatus::NotFound ? "unset" : "error");
    if (!observing()) return;
    // NIFT_HOOK_* values are invocation-local facts about the tracked item
    // (target, paths, phase) supplied by the build itself, not the process
    // environment; they change only with tracked metadata, which already
    // invalidates the page.
    if (name.rfind("NIFT_HOOK_", 0) == 0) return;
    if (value.status == nift::HostStatus::Found) result_.observed.env[name] = value.value;
    else if (value.status == nift::HostStatus::NotFound) result_.observed.env[name] = std::nullopt;
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
