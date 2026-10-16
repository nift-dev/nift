// Runtime 2.0 CP02: dynamic reads that can change rendered bytes are recorded
// on the page's RenderResult so the incremental build engine can re-check
// them. Standalone scripts record nothing: there is no page to invalidate.
#include "Parser.h"
#include "ReadObservation.h"

namespace fs = std::filesystem;

Parser::FsInfo Parser::inspect_path(const fs::path& path) const {
    FsInfo info = inspect_path_unobserved(path);
    if (!info.error) observe_presence(path, info.exists);
    return info;
}

void Parser::observe_presence(const fs::path& path, bool exists) const {
    if (standalone_script_host_ || host_.root().empty()) return;
    // inspect_path is const for its callers, but the Parser itself is never a
    // const object while rendering; recording an observation is not a change
    // to the evaluation state that const protects.
    auto& result = const_cast<Parser*>(this)->result_;
    const std::string spelling = nift::observe::spell(host_.root(), path);
    if (exists) result.reqs.insert(spelling);       // stale if it disappears
    else result.observed.absent.insert(spelling);   // stale if it appears
}

void Parser::observe_listing(const std::string& key, const fs::path&) {
    if (standalone_script_host_ || host_.root().empty()) return;
    result_.observed.listings[key] = nift::observe::listing_fingerprint(host_.root(), key);
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
