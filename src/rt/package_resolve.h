#pragma once
// Package imports on the VM (D-NEXT-packages; checkpoints/weekend-packages.md
// §2, §4.1): `import("name")` resolves against the project at the current
// directory with legacy's steps, order and texts (src/ParserScript.cpp
// execute_import_file). The shared package code (PackageTransaction,
// PackageMetadata.h, PackageGraphLock.h) is used, never copied: the read lock
// is PackageTransaction::acquire_read_existing_store, which never creates
// `.nift/`. One PackageReader holds the lock for a whole graph build (one
// snapshot) and releases it on destruction, on every path.
#include <filesystem>
#include <memory>
#include <string>

class PackageTransaction;

namespace nift::rt {

// Legacy's test: no `/` or `\` and no extension (empty included).
bool package_shaped(const std::string& argument);

struct PackageResolution {
    bool ok = false;
    bool lock_error = false;          // a lock acquisition failure (import-error:package-lock)
    std::string error;                // legacy's text, without frame prefixes
    std::filesystem::path root;       // .nift/packages/<name>
    std::filesystem::path entry;      // absolute, normalised
};

class PackageReader {
public:
    explicit PackageReader(std::filesystem::path importer_dir = {});  // the argument: test-only (mutants.sh)
    ~PackageReader();
    PackageReader(const PackageReader&) = delete;
    PackageReader& operator=(const PackageReader&) = delete;
    PackageResolution resolve(const std::string& name);
    void release();  // ends the critical section (also on destruction)

private:
    bool acquire(PackageResolution& out);
    std::filesystem::path project_;
    std::unique_ptr<PackageTransaction> lock_;
    bool attempted_ = false, store_present_ = false;
    std::string lock_failure_;
};

} // namespace nift::rt
