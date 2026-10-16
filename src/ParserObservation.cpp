// Runtime 2.0 CP02: dynamic reads that can change rendered bytes are recorded
// on the page's RenderResult so the incremental build engine can re-check
// them. Standalone scripts record nothing: there is no page to invalidate.
#include "Parser.h"
#include "ReadObservation.h"

namespace fs = std::filesystem;

Parser::FsInfo Parser::inspect_path(const fs::path& path) const {
    FsInfo info = inspect_path_unobserved(path);
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
    if (standalone_script_host_ || host_.root().empty()) return;
    result_.dependencies.insert(host_.relative(path));
    host_.observe_dependency(path, bytes);
}

void Parser::observe_metadata_read(const fs::path& path) {
    if (standalone_script_host_ || host_.root().empty()) return;
    result_.dependencies.insert(host_.relative(path));
    host_.observe_dependency_hash(path, filesystem::hash_path(path));
}

void Parser::note_written(const fs::path& path) {
    if (standalone_script_host_ || host_.root().empty()) return;
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    result_.observed.written.insert((error ? path : absolute).lexically_normal());
}

void Parser::observe_listing(const std::string& key, const fs::path&) {
    if (standalone_script_host_ || host_.root().empty()) return;
    result_.observed.listings[key] = host_.listing_fingerprint(key);
}

void Parser::observe_environment(const std::string& name, const nift::HostResult& value) {
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
