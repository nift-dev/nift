// CP16: the engine-neutral dependency recorder (PageObserver.h). The rules
// are host-deps' (D-028), moved here unchanged from ParserObservation.cpp.
#include "PageObserver.h"

#include "FileSystem.h"
#include "ReadObservation.h"

namespace fs = std::filesystem;

void PageObserver::record_hash(const fs::path& path, std::string_view bytes) {
    if (host_.observe_dependencies)
        host_.observe_dependency_hash(path, filesystem::hash_bytes_update(filesystem::kHashBytesSeed, bytes.data(), bytes.size()));
}

void PageObserver::presence(const fs::path& path, bool exists, std::string_view kind) {
    if (!mode_.observing) return;
    const std::string spelling = nift::observe::spell(host_.root(), path);
    if (exists) {
        result_.reqs.insert(spelling);                                      // stale if it disappears
        if (!kind.empty()) result_.observed.kinds[spelling] = std::string(kind);  // stale if its type changes
    } else {
        result_.observed.absent.insert(spelling);                          // stale if it appears
    }
}

void PageObserver::read(const fs::path& path, std::string_view bytes) {
    if (!mode_.observing) return;
    result_.dependencies.insert(host_.relative(path));
    record_hash(path, bytes);
}

void PageObserver::open_read(const fs::path& path, std::string_view bytes) {
    if (mode_.observing) { read(path, bytes); return; }
    // c7971365: a post-build hook (or standalone script) still reports what
    // file().open read, so a declared dependency it changed is detected.
    record_hash(path, bytes);
}

void PageObserver::failed_read(const fs::path& path) {
    if (!mode_.observing) return;
    std::error_code error;
    const auto status = fs::status(path, error);
    if ((!error && !fs::exists(status)) ||
        (error && (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory))) {
        presence(path, false, {});
        return;
    }
    result_.observed.unreadable[nift::observe::spell(host_.root(), path)] = filesystem::read_access_error(path);
}

void PageObserver::stream_open(const fs::path& path) {
    if (!mode_.observing) return;
    result_.dependencies.insert(host_.relative(path));
    // Hashed at open: an edit made while the page streams the file leaves the
    // recorded snapshot different from the file, so the next build rebuilds.
    if (snapshot_wanted()) host_.observe_dependency_hash(path, filesystem::hash_path_streamed(path));
}

void PageObserver::streamed_read(const fs::path& path, std::uint64_t, const std::uint64_t* hash) {
    if (!mode_.observing) return;
    result_.dependencies.insert(host_.relative(path));
    if (hash) host_.observe_dependency_hash(path, *hash);
}

void PageObserver::metadata_read(const fs::path& path) {
    if (!mode_.observing) return;
    result_.dependencies.insert(host_.relative(path));
    host_.observe_dependency_hash(path, filesystem::hash_path(path));
}

void PageObserver::written(const fs::path& path) {
    if (!mode_.observing) return;
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    result_.observed.written.insert((error ? path : absolute).lexically_normal());
}

void PageObserver::listing(const std::string& key, const fs::path&) {
    if (!mode_.observing) return;
    result_.observed.listings[key] = host_.listing_fingerprint(key);
}

void PageObserver::environment(const std::string& name, const std::optional<std::string>& value) {
    if (!mode_.observing) return;
    // NIFT_HOOK_* values are invocation-local facts about the tracked item
    // (target, paths, phase) supplied by the build itself, not the process
    // environment; they change only with tracked metadata, which already
    // invalidates the page.
    if (name.rfind("NIFT_HOOK_", 0) == 0) return;
    result_.observed.env[name] = value;
}

// env() and platform() keep legacy's own conditions (any non-standalone
// execution, not only observing ones).
void PageObserver::environment_snapshot() {
    if (!mode_.standalone) result_.observed.environment_snapshot = true;
}

void PageObserver::platform(const std::string& value) {
    if (!mode_.standalone && !host_.root().empty()) result_.observed.platform = value;
}
