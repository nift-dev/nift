// Runtime 2.0 CP02: dynamic reads that can change rendered bytes are recorded
// on the page's RenderResult so the incremental build engine can re-check
// them. Standalone scripts record nothing: there is no page to invalidate.
#include "Parser.h"
#include "ReadObservation.h"
#include "HostTrace.h"

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
    if (standalone_script_host_ || host_.root().empty()) return;
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
    if (standalone_script_host_ || host_.root().empty()) return;
    result_.dependencies.insert(host_.relative(path));
    host_.observe_dependency(path, bytes);
}

void Parser::observe_streamed_read(const fs::path& path, std::uint64_t size, const std::uint64_t* snapshot) {
    if (nift::trace::enabled()) trace_host("read", path, std::to_string(size));
    if (standalone_script_host_ || host_.root().empty()) return;
    result_.dependencies.insert(host_.relative(path));
    if (snapshot) host_.observe_dependency_hash(path, *snapshot);
}

bool Parser::read_snapshot_wanted() const {
    return !standalone_script_host_ && !host_.root().empty() && host_.observe_dependencies;
}

void Parser::observe_metadata_read(const fs::path& path) {
    if (standalone_script_host_ || host_.root().empty()) return;
    result_.dependencies.insert(host_.relative(path));
    host_.observe_dependency_hash(path, filesystem::hash_path(path));
}

void Parser::note_written(const fs::path& path, const char* op) {
    if (nift::trace::enabled()) trace_host(op, path, "");
    if (standalone_script_host_ || host_.root().empty()) return;
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    result_.observed.written.insert((error ? path : absolute).lexically_normal());
}

void Parser::observe_listing(const std::string& key, const fs::path& path) {
    if (nift::trace::enabled()) trace_host(key.rfind("glob:", 0) == 0 ? "glob" : "list", path, "");
    if (standalone_script_host_ || host_.root().empty()) return;
    result_.observed.listings[key] = host_.listing_fingerprint(key);
}

void Parser::observe_environment(const std::string& name, const nift::HostResult& value) {
    if (nift::trace::enabled())
        nift::trace::emit("getenv", name, value.status == nift::HostStatus::Found ? "found" :
                                          value.status == nift::HostStatus::NotFound ? "unset" : "error");
    if (standalone_script_host_ || host_.root().empty()) return;
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
