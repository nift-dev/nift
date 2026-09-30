#include "PackageTransaction.h"

#include "FileSystem.h"
#include "JsonFile.h"
#include "PackageMetadata.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <set>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

bool open_and_lock(const fs::path& path, bool exclusive, void*& file) {
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        CloseHandle(handle);
        return false;
    }
    OVERLAPPED overlapped{};
    if (!LockFileEx(handle, exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0, 0, 1, 0, &overlapped)) {
        CloseHandle(handle);
        return false;
    }
    file = static_cast<void*>(handle);
    return true;
#else
    const int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0644);
    if (fd < 0) return false;
    struct stat status{};
    if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || ::flock(fd, exclusive ? LOCK_EX : LOCK_SH) != 0) {
        ::close(fd);
        return false;
    }
    file = reinterpret_cast<void*>(static_cast<std::intptr_t>(fd) + 1);
    return true;
#endif
}

bool lock_path_matches(const fs::path& path, void* file) {
#ifdef _WIN32
    (void)path;
    BY_HANDLE_FILE_INFORMATION info{};
    return file && GetFileInformationByHandle(static_cast<HANDLE>(file), &info) &&
           (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0;
#else
    if (!file) return false;
    struct stat opened{}, named{};
    const int fd = static_cast<int>(reinterpret_cast<std::intptr_t>(file) - 1);
    return ::fstat(fd, &opened) == 0 && ::lstat(path.c_str(), &named) == 0 &&
           opened.st_dev == named.st_dev && opened.st_ino == named.st_ino && S_ISREG(named.st_mode);
#endif
}

void close_lock(void*& file) {
    if (!file) return;
#ifdef _WIN32
    OVERLAPPED overlapped{};
    UnlockFileEx(static_cast<HANDLE>(file), 0, 1, 0, &overlapped);
    CloseHandle(static_cast<HANDLE>(file));
#else
    const int fd = static_cast<int>(reinterpret_cast<std::intptr_t>(file) - 1);
    ::flock(fd, LOCK_UN);
    ::close(fd);
#endif
    file = nullptr;
}

bool exists_no_follow(const fs::path& path) {
    std::error_code error;
    return fs::symlink_status(path, error).type() != fs::file_type::not_found && !error;
}

bool rename_path(const fs::path& source, const fs::path& destination, std::string& error) {
    std::error_code ec;
    fs::rename(source, destination, ec);
    if (!ec) return true;
    error = "cannot rename '" + source.generic_string() + "' to '" + destination.generic_string() + "': " + ec.message();
    return false;
}

std::string current_contents(const fs::path& path, bool& missing, std::string& error) {
    missing = !filesystem::path_exists(path);
    if (missing) return {};
    const auto contents = filesystem::read_file_checked(path);
    if (!contents) error = "cannot read transaction metadata: " + path.generic_string();
    return contents.value_or(std::string());
}

bool metadata_matches(const fs::path& path, const json::Document& old_value,
                      const std::string& desired, std::string& error) {
    bool missing = false;
    const std::string current = current_contents(path, missing, error);
    if (!error.empty()) return false;
    const bool old_missing = old_value.is_null();
    const bool matches_old = old_missing ? missing : old_value.is_string() && !missing && current == old_value.string;
    const bool matches_desired = !missing && current == desired;
    if (matches_old || matches_desired) return true;
    error = "package transaction metadata changed outside recovery: " + path.generic_string();
    return false;
}

void crash_at(const char* point) {
    const char* requested = std::getenv("NIFT_TEST_PACKAGE_TXN_CRASH");
    if (!requested || std::string(requested) != point) return;
#ifdef _WIN32
    ExitProcess(86);
#else
    std::_Exit(86);
#endif
}

void test_hold_after_acquire() {
    const char* hold = std::getenv("NIFT_TEST_PACKAGE_TXN_HOLD");
    if (!hold || !*hold) return;
    const fs::path root(hold);
    std::error_code ignored;
    { std::ofstream(root/"acquired"); }
    while (!fs::exists(root/"release", ignored)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    fs::remove(root/"acquired", ignored);
}

void test_hold_after_stage() {
    const char* hold = std::getenv("NIFT_TEST_PACKAGE_TXN_STAGE_HOLD");
    if (!hold || !*hold) return;
    const fs::path root(hold);
    std::error_code ignored;
    { std::ofstream(root/"staged"); }
    while (!fs::exists(root/"release", ignored)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    fs::remove(root/"staged", ignored);
}

bool valid_transaction_id(const std::string& id) {
    if (id.rfind(".txn-", 0) != 0 || id.size() <= 5) return false;
    return std::all_of(id.begin() + 5, id.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || c == '-';
    });
}

bool real_directory(const fs::path& path) {
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    return !ec && status.type() == fs::file_type::directory;
}

bool journal_file_state(const fs::path& path, bool& present, std::string& error) {
    std::error_code ec;
    const auto status=fs::symlink_status(path,ec);
    if(status.type()==fs::file_type::not_found){present=false;return true;}
    if(ec){error="cannot inspect package transaction journal: "+ec.message();return false;}
    if(status.type()!=fs::file_type::regular){error="package transaction journal is not a regular file";return false;}
    present=true;return true;
}

bool detached_git_head(const fs::path& package, const std::string& expected) {
    const auto head = filesystem::read_file_checked(package/".git"/"HEAD");
    if (!head) return false;
    std::string value = *head;
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value == expected;
}

}  // namespace

PackageTransaction::PackageTransaction(fs::path project_root)
    : project_root_(fs::absolute(std::move(project_root)).lexically_normal()),
      nift_root_(project_root_/".nift"), package_root_(nift_root_/"packages"),
      journal_path_(nift_root_/"package-transaction.json") {}

PackageTransaction::~PackageTransaction() {
    if (locked_ && !journal_published_ && !staging_root_.empty()) {
        std::error_code ignored;
        fs::remove_all(staging_root_, ignored);
    }
    release();
}

void PackageTransaction::release() {
    close_lock(lock_file_);
    locked_ = false;
}

bool PackageTransaction::lock_identity_valid() const {
    return locked_ && lock_path_matches(nift_root_/".packages-transaction.lock", lock_file_);
}

bool PackageTransaction::acquire(std::string& error) {
    if (locked_) return true;
    std::error_code ec;
    fs::create_directories(package_root_, ec);
    if (ec) { error = "cannot create package transaction directories: " + ec.message(); return false; }
    if (!open_and_lock(nift_root_/".packages-transaction.lock", true, lock_file_)) {
        error = "cannot acquire package transaction lock";
        return false;
    }
    locked_ = true;exclusive_ = true;
    test_hold_after_acquire();
    if(!recover(error))return false;
    old_manifest_=current_contents(project_root_/"manifest.json",old_manifest_missing_,error);if(!error.empty())return false;
    old_lock_=current_contents(nift_root_/"packages.lock.json",old_lock_missing_,error);if(!error.empty())return false;
    return true;
}

bool PackageTransaction::acquire_read(std::string& error) {
    if (locked_) return !exclusive_;
    std::error_code ec;fs::create_directories(nift_root_,ec);
    if(ec){error="cannot create package lock directory: "+ec.message();return false;}
    if(!open_and_lock(nift_root_/".packages-transaction.lock",false,lock_file_)){error="cannot acquire package read lock";return false;}
    locked_=true;exclusive_=false;
    if(!lock_identity_valid()){error="package read lock identity changed";return false;}
    bool journal_present=false;if(!journal_file_state(journal_path_,journal_present,error))return false;
    if(journal_present){error="unfinished package transaction; run a package command to recover it";return false;}
    return true;
}

bool PackageTransaction::create_staging(fs::path& staging_root, std::string& error) {
    if (!locked_) { error = "package transaction lock is not held"; return false; }
    static std::atomic<unsigned long long> counter{0};
#ifdef _WIN32
    const auto pid = static_cast<unsigned long long>(GetCurrentProcessId());
#else
    const auto pid = static_cast<unsigned long long>(::getpid());
#endif
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto serial = counter.fetch_add(1, std::memory_order_relaxed);
        const auto tick = static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
        const fs::path candidate = package_root_/(".txn-" + std::to_string(pid) + "-" + std::to_string(tick) + "-" + std::to_string(serial));
        std::error_code ec;
        if (fs::create_directory(candidate, ec)) {
            fs::create_directories(candidate/"new", ec);
            if (!ec) fs::create_directories(candidate/"old", ec);
            if (ec) { fs::remove_all(candidate, ec); error = "cannot create package transaction staging"; return false; }
            staging_root_ = candidate;
            staging_root = candidate;
            return true;
        }
        if (ec && ec != std::errc::file_exists) { error = "cannot create package transaction staging: " + ec.message(); return false; }
    }
    error = "cannot allocate a unique package transaction directory";
    return false;
}

bool PackageTransaction::commit(const std::vector<Operation>& operations,
                                const json::Document& manifest,
                                const json::Document& lock,
                                std::string& error) {
    if (!locked_ || staging_root_.empty()) { error = "package transaction is not prepared"; return false; }
    if (!exclusive_ || !lock_path_matches(nift_root_/".packages-transaction.lock", lock_file_)) { error = "package transaction lock identity changed"; return false; }
    package_metadata::Manifest parsed_manifest;
    package_metadata::Lock parsed_lock;
    if (!package_metadata::parse_manifest(manifest, false, parsed_manifest, error) ||
        !package_metadata::parse_lock(lock, parsed_lock, error) ||
        !package_metadata::validate_lock(parsed_manifest, parsed_lock, error)) return false;

    std::vector<Operation> sorted = operations;
    std::sort(sorted.begin(), sorted.end(), [](const Operation& a, const Operation& b) { return a.name < b.name; });
    std::set<std::string> names;
    json::Document journal = json::Document::make_object();
    journal["version"] = json::Document(1);
    journal["transaction"] = json::Document(staging_root_.filename().string());
    journal["operations"] = json::Document::make_array();
    if(sorted.empty()){error="package transaction has no operations";return false;}
    for (const auto& operation : sorted) {
        if (!filesystem::valid_package_name(operation.name) || !names.insert(operation.name).second) { error = "invalid or duplicate package transaction operation"; return false; }
        if (operation.kind == Kind::Replace && !exists_no_follow(staging_root_/"new"/operation.name)) { error = "staged package is missing: " + operation.name; return false; }
        if(operation.kind==Kind::Replace){const fs::path staged=staging_root_/"new"/operation.name;const auto desired=parsed_lock.find(operation.name);if(desired==parsed_lock.end()){error="replacement missing from desired package lock: "+operation.name;return false;}if(desired->second.commit!="local"&&!real_directory(staged)){error="remote staged package must be a real directory: "+operation.name;return false;}package_metadata::Manifest staged_manifest;fs::path staged_entry;if(!package_metadata::load_package(staged,staged_manifest,staged_entry,error)||staged_manifest.name!=operation.name){if(error.empty())error="staged package identity mismatch: "+operation.name;return false;}}
        json::Document item = json::Document::make_object();
        item["name"] = json::Document(operation.name);
        item["kind"] = json::Document(operation.kind == Kind::Replace ? "replace" : "remove");
        item["hadOld"] = json::Document(exists_no_follow(package_root_/operation.name));
        const auto locked=parsed_lock.find(operation.name);
        item["commit"] = json::Document(operation.kind==Kind::Replace?locked->second.commit:"removed");
        journal["operations"].array.push_back(std::move(item));
    }
    journal["manifest"] = manifest;
    journal["lock"] = lock;
    test_hold_after_stage();
    bool missing=false;std::string read_error;
    const std::string current_manifest=current_contents(project_root_/"manifest.json",missing,read_error);if(!read_error.empty()){error=read_error;return false;}
    if(missing!=old_manifest_missing_||(!missing&&current_manifest!=old_manifest_)){error="manifest.json changed while packages were staged";return false;}
    const std::string current_lock=current_contents(nift_root_/"packages.lock.json",missing,read_error);if(!read_error.empty()){error=read_error;return false;}
    if(missing!=old_lock_missing_||(!missing&&current_lock!=old_lock_)){error="package lock changed while packages were staged";return false;}
    journal["oldManifest"] = old_manifest_missing_ ? json::Document(nullptr) : json::Document(old_manifest_);
    journal["oldLock"] = old_lock_missing_ ? json::Document(nullptr) : json::Document(old_lock_);

    crash_at("after-stage");
    if (!save_json_file(journal_path_, journal)) { error = "cannot publish package transaction journal"; return false; }
    journal_published_ = true;
    crash_at("after-journal");
    return apply_journal(journal, error);
}

bool PackageTransaction::recover(std::string& error) {
    bool journal_present=false;if(!journal_file_state(journal_path_,journal_present,error))return false;
    if (journal_present) {
        json::Document journal;
        if (!load_json_file(journal_path_, journal, error)) { error = "cannot read package transaction journal: " + error; return false; }
        journal_published_ = true;
        return apply_journal(journal, error);
    }
    std::error_code ec;std::vector<fs::path> abandoned;
    for (fs::directory_iterator it(package_root_, ec), end; !ec && it != end; it.increment(ec)) if(it->path().filename().string().rfind(".txn-",0)==0)abandoned.push_back(it->path());
    if (ec) { error = "cannot inspect package transaction staging: " + ec.message(); return false; }
    for(const auto& path:abandoned){fs::remove_all(path,ec);if(ec){error="cannot remove abandoned package staging: "+ec.message();return false;}}
    return true;
}

bool PackageTransaction::apply_journal(const json::Document& journal, std::string& error) {
    if (!package_metadata::exact_fields(journal, {"version", "transaction", "operations", "manifest", "lock", "oldManifest", "oldLock"}, "package transaction journal", error)) return false;
    if (!journal.has("version") || !journal["version"].is_number() || journal["version"].num != 1 ||
        !journal.has("transaction") || !journal["transaction"].is_string() || !valid_transaction_id(journal["transaction"].string) ||
        !journal.has("operations") || !journal["operations"].is_array() || !journal.has("manifest") || !journal.has("lock") ||
        !journal.has("oldManifest") || !(journal["oldManifest"].is_null() || journal["oldManifest"].is_string()) ||
        !journal.has("oldLock") || !(journal["oldLock"].is_null() || journal["oldLock"].is_string())) {
        error = "invalid package transaction journal"; return false;
    }
    package_metadata::Manifest manifest;
    package_metadata::Lock lock;
    if (!package_metadata::parse_manifest(journal["manifest"], false, manifest, error) ||
        !package_metadata::parse_lock(journal["lock"], lock, error) ||
        !package_metadata::validate_lock(manifest, lock, error)) return false;

    const std::string manifest_text = journal["manifest"].dump(2) + "\n";
    const std::string lock_text = journal["lock"].dump(2) + "\n";
    if (!metadata_matches(project_root_/"manifest.json", journal["oldManifest"], manifest_text, error) ||
        !metadata_matches(nift_root_/"packages.lock.json", journal["oldLock"], lock_text, error)) return false;

    if(!lock_path_matches(nift_root_/".packages-transaction.lock",lock_file_)){error="package transaction lock identity changed";return false;}
    struct ParsedOperation { std::string name; Kind kind; bool had_old; std::string commit; };
    std::vector<ParsedOperation> operations;
    std::set<std::string> names;
    for (const auto& value : journal["operations"].array) {
        if (!package_metadata::exact_fields(value, {"name", "kind", "hadOld", "commit"}, "package transaction operation", error) ||
            !value.has("name") || !value["name"].is_string() || !filesystem::valid_package_name(value["name"].string) ||
            !value.has("kind") || !value["kind"].is_string() || (value["kind"].string != "replace" && value["kind"].string != "remove") ||
            !value.has("hadOld") || !value["hadOld"].is_bool() || !value.has("commit") || !value["commit"].is_string() || !names.insert(value["name"].string).second) {
            error = "invalid package transaction operation"; return false;
        }
        const Kind kind=value["kind"].string=="replace"?Kind::Replace:Kind::Remove;const auto desired_lock=lock.find(value["name"].string);
        if((kind==Kind::Replace&&(desired_lock==lock.end()||desired_lock->second.commit!=value["commit"].string))||(kind==Kind::Remove&&(desired_lock!=lock.end()||value["commit"].string!="removed"))){error="package transaction operation does not match desired lock";return false;}
        operations.push_back({value["name"].string,kind,value["hadOld"].boolean,value["commit"].string});
    }
    if(operations.empty()){error="package transaction has no operations";return false;}
    std::sort(operations.begin(), operations.end(), [](const ParsedOperation& a, const ParsedOperation& b) { return a.name < b.name; });
    const fs::path transaction_root = package_root_/journal["transaction"].string;
    if (transaction_root.parent_path() != package_root_) { error = "package transaction path escapes the package store"; return false; }
    if(!real_directory(transaction_root)||!real_directory(transaction_root/"new")||!real_directory(transaction_root/"old")){error="package transaction staging layout is missing or redirected";return false;}

    package_metadata::Lock old_lock;
    if(journal["oldLock"].is_string()){
        json::Document old_lock_document;std::string parse_error;
        if(!nift_json::parse(journal["oldLock"].string,old_lock_document,parse_error)||!package_metadata::parse_lock(old_lock_document,old_lock,error)){error="invalid old package lock in transaction journal";return false;}
    }
    std::map<std::string,Kind> operation_kinds;for(const auto& operation:operations)operation_kinds.emplace(operation.name,operation.kind);
    std::set<std::string> lock_names;for(const auto& item:old_lock)lock_names.insert(item.first);for(const auto& item:lock)lock_names.insert(item.first);
    auto lock_equal=[](const package_metadata::LockEntry& a,const package_metadata::LockEntry& b){return a.source==b.source&&a.requested==b.requested&&a.commit==b.commit;};
    for(const auto& name:lock_names){const auto before=old_lock.find(name),after=lock.find(name);const bool changed=before==old_lock.end()||after==lock.end()||!lock_equal(before->second,after->second);if(!changed)continue;const auto operation=operation_kinds.find(name);const Kind required=after==lock.end()?Kind::Remove:Kind::Replace;if(operation==operation_kinds.end()||operation->second!=required){error="package transaction operations do not cover lock change: "+name;return false;}}

    // Recovery independently validates every replacement before moving any old
    // live slot. A post-promotion retry validates the live replacement instead.
    for(const auto& operation:operations){if(operation.kind==Kind::Remove)continue;const fs::path staged=transaction_root/"new"/operation.name;const fs::path live=package_root_/operation.name;const fs::path candidate=exists_no_follow(staged)?staged:live;if(operation.commit!="local"&&!real_directory(candidate)){error="remote package replacement must be a real directory: "+operation.name;return false;}package_metadata::Manifest candidate_manifest;fs::path candidate_entry;if(!exists_no_follow(candidate)||!package_metadata::load_package(candidate,candidate_manifest,candidate_entry,error)||candidate_manifest.name!=operation.name){if(error.empty())error="package replacement identity mismatch: "+operation.name;return false;}if(operation.commit!="local"&&!detached_git_head(candidate,operation.commit)){error="package replacement revision mismatch: "+operation.name;return false;}}

    enum class SlotState { Before, BackedUp, Promoted, Removed };
    std::map<std::string,SlotState> states;
    for(const auto& operation:operations){const fs::path live=package_root_/operation.name,staged=transaction_root/"new"/operation.name,backup=transaction_root/"old"/operation.name;const bool l=exists_no_follow(live),s=exists_no_follow(staged),b=exists_no_follow(backup);SlotState state;
        if(operation.kind==Kind::Replace&&operation.had_old&&l&&s&&!b)state=SlotState::Before;
        else if(operation.kind==Kind::Replace&&operation.had_old&&!l&&s&&b)state=SlotState::BackedUp;
        else if(operation.kind==Kind::Replace&&operation.had_old&&l&&!s&&b)state=SlotState::Promoted;
        else if(operation.kind==Kind::Replace&&!operation.had_old&&!l&&s&&!b)state=SlotState::Before;
        else if(operation.kind==Kind::Replace&&!operation.had_old&&l&&!s&&!b)state=SlotState::Promoted;
        else if(operation.kind==Kind::Remove&&operation.had_old&&l&&!s&&!b)state=SlotState::Before;
        else if(operation.kind==Kind::Remove&&operation.had_old&&!l&&!s&&b)state=SlotState::Removed;
        else if(operation.kind==Kind::Remove&&!operation.had_old&&!l&&!s&&!b)state=SlotState::Removed;
        else {error="invalid package transaction slot state: "+operation.name;return false;}
        states.emplace(operation.name,state);
    }

    for (const auto& operation : operations) {
        const fs::path live = package_root_/operation.name;
        const fs::path backup = transaction_root/"old"/operation.name;
        if(states.at(operation.name)==SlotState::Before&&operation.had_old&&!rename_path(live,backup,error))return false;
    }
    crash_at("after-backup");

    for (const auto& operation : operations) {
        if (operation.kind == Kind::Remove) continue;
        const fs::path live = package_root_/operation.name;
        const fs::path staged = transaction_root/"new"/operation.name;
        if (exists_no_follow(staged)) {
            if (exists_no_follow(live)) { error = "package destination exists during promotion: " + operation.name; return false; }
            if (!rename_path(staged, live, error)) return false;
        } else if (!exists_no_follow(live)) { error = "package transaction lost the staged package: " + operation.name; return false; }
    }
    crash_at("after-promote");

    if (!save_json_file(project_root_/"manifest.json", journal["manifest"])) { error = "cannot publish package manifest"; return false; }
    crash_at("after-manifest");
    if (!save_json_file(nift_root_/"packages.lock.json", journal["lock"])) { error = "cannot publish package lock"; return false; }
    crash_at("after-lock");

    package_metadata::Manifest verified_manifest;
    package_metadata::Lock verified_lock; bool lock_exists = false;
    if (!package_metadata::load_manifest(project_root_/"manifest.json", false, verified_manifest, error) ||
        !package_metadata::load_lock(nift_root_/"packages.lock.json", verified_lock, lock_exists, error) || !lock_exists ||
        !package_metadata::validate_lock(verified_manifest, verified_lock, error)) return false;
    for (const auto& operation : operations) {
        const fs::path live = package_root_/operation.name;
        if (operation.kind == Kind::Remove) {
            if (exists_no_follow(live)) { error = "removed package slot still exists: " + operation.name; return false; }
            continue;
        }
        package_metadata::Manifest installed;fs::path entry;
        if (!package_metadata::load_package(live, installed, entry, error) || installed.name != operation.name) { if(error.empty())error="installed package identity mismatch: "+operation.name;return false; }
        if(operation.commit!="local"&&!detached_git_head(live,operation.commit)){error="installed package revision mismatch: "+operation.name;return false;}
    }

    bool metadata_missing=false;std::string metadata_error;
    if(current_contents(project_root_/"manifest.json",metadata_missing,metadata_error)!=manifest_text||metadata_missing||!metadata_error.empty()){error="published package manifest does not match transaction";return false;}
    if(current_contents(nift_root_/"packages.lock.json",metadata_missing,metadata_error)!=lock_text||metadata_missing||!metadata_error.empty()){error="published package lock does not match transaction";return false;}

    std::error_code ec;
    fs::remove(journal_path_, ec);
    if (ec) { error = "cannot remove package transaction journal: " + ec.message(); return false; }
    journal_published_ = false;
    crash_at("during-cleanup");
    fs::remove_all(transaction_root, ec);
    if (ec) { error = "cannot clean package transaction directory: " + ec.message(); return false; }
    staging_root_.clear();
    return true;
}
