#include "rt/package_resolve.h"

#include "FileSystem.h"
#include "PackageGraphLock.h"
#include "PackageMetadata.h"
#include "PackageTransaction.h"

// rt-lint: cold-file package resolution, once per package import

namespace nift::rt {

namespace fs = std::filesystem;

bool package_shaped(const std::string& argument) {
    return argument.find('/') == std::string::npos && argument.find('\\') == std::string::npos &&
           fs::path(argument).extension().empty();
}

PackageReader::PackageReader(fs::path importer_dir) : project_(fs::absolute(fs::current_path()).lexically_normal()) {
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 1  // test-only (packages/mutants.sh)
    if (!importer_dir.empty()) project_ = fs::absolute(importer_dir).lexically_normal();
#endif
    (void)importer_dir;
}

PackageReader::~PackageReader() { release(); }

void PackageReader::release() {
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 4  // test-only (packages/mutants.sh)
    lock_.release();
#else
    lock_.reset();
#endif
}

bool PackageReader::acquire(PackageResolution& out) {
    if (!attempted_) {
        attempted_ = true;
        lock_ = std::make_unique<PackageTransaction>(project_);
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 2  // test-only (packages/mutants.sh)
        store_present_ = true;
        if (!lock_->acquire_read(lock_failure_)) {
#elif defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 5  // test-only (packages/mutants.sh)
        store_present_ = fs::exists(project_/".nift");
        if (false) {
#else
        if (!lock_->acquire_read_existing_store(lock_failure_, store_present_)) {
#endif
            lock_.reset();
            if (lock_failure_.empty()) lock_failure_ = "cannot acquire package read lock";
        }
    }
    if (!lock_failure_.empty()) {
        out.lock_error = true;
        out.error = lock_failure_;
        return false;
    }
    return true;
}

// The steps of legacy's execute_import_file for a package name, in order.
PackageResolution PackageReader::resolve(const std::string& name) {
    PackageResolution out;
    if (!filesystem::valid_package_name(name)) { out.error = "invalid package name: " + name; return out; }
    if (!acquire(out)) return out;
    const fs::path packages = (project_/".nift"/"packages").lexically_normal();
    const fs::path root = (packages/name).lexically_normal();
    if (!store_present_) {  // no `.nift/`: nothing installed, and nothing is created
        out.error = "package is not installed: " + name;
        return out;
    }
    if (!filesystem::direct_package_store(project_)) {
        out.error = "package store must stay inside the project";
        return out;
    }
    if (root.parent_path() != packages) { out.error = "package path escapes .nift/packages: " + name; return out; }
    if (!filesystem::file_exists(root/"manifest.json")) { out.error = "package is not installed: " + name; return out; }
    package_metadata::Manifest manifest;
    std::string detail;
    if (!package_metadata::load_manifest(root/"manifest.json", true, manifest, detail)) {
        out.error = "invalid package manifest: " + name + ": " + detail;
        return out;
    }
    const fs::path entry = (root/manifest.entry).lexically_normal();
    if (!filesystem::path_within(root, entry)) {
        out.error = "package entry escapes the package directory: " + manifest.entry;
        return out;
    }
    if (manifest.name != name) { out.error = "installed package name does not match import: " + name; return out; }
    std::string locked_source, locked_commit, lock_error;
    bool locked_found = false;
    if (!package_graph::lock_node_identity(project_/".nift"/"packages.lock.json", name, locked_source, locked_commit,
                                           locked_found, lock_error) || !locked_found) {
        out.error = lock_error.empty() ? "package lock is missing dependency: " + name : lock_error;
        return out;
    }
    if (locked_commit != "local") {
        std::string revision = filesystem::read_file_checked(root/".git"/"HEAD").value_or(std::string());
        while (!revision.empty() && (revision.back() == '\n' || revision.back() == '\r')) revision.pop_back();
        if (revision != locked_commit) {
            out.error = "installed package revision does not match package lock: " + name;
            return out;
        }
    }
    out.ok = true;
    out.root = root;
    out.entry = entry;
    return out;
}

} // namespace nift::rt
