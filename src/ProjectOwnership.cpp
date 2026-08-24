#include "ProjectOwnership.h"

#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace {

// Creates the marker with exclusive-create semantics: returns true when this
// call created a brand-new file, false when the file already existed.
// On failure leaves errno (POSIX) / GetLastError (Windows) set.
bool exclusive_create(const std::filesystem::path& path, void*& file) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        file = static_cast<void*>(h);
        return true;
    }
    if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    // Fall through: the caller re-opens the existing file with OPEN_EXISTING.
    file = nullptr;
    return false;
#else
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0644);
    if (fd >= 0) {
        file = reinterpret_cast<void*>(static_cast<std::intptr_t>(fd));
        return true;
    }
    return false;
#endif
}

// Opens an existing marker (no create) for the advisory lock.
bool open_existing(const std::filesystem::path& path, void*& file) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    file = static_cast<void*>(h);
    return true;
#else
    const int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) return false;
    file = reinterpret_cast<void*>(static_cast<std::intptr_t>(fd));
    return true;
#endif
}

// Takes the exclusive advisory lock; returns true when acquired, false when a
// live process holds it (or on error).
bool try_lock(void* file) {
#ifdef _WIN32
    OVERLAPPED overlapped{};
    return LockFileEx(static_cast<HANDLE>(file),
                      LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                      0, 1, 0, &overlapped) != 0;
#else
    return ::flock(static_cast<int>(reinterpret_cast<std::intptr_t>(file)),
                   LOCK_EX | LOCK_NB) == 0;
#endif
}

bool durable_sync(void* file) {
#ifdef _WIN32
    return FlushFileBuffers(static_cast<HANDLE>(file)) != 0;
#else
    return ::fsync(static_cast<int>(reinterpret_cast<std::intptr_t>(file))) == 0;
#endif
}

void close_file(void*& file) {
#ifdef _WIN32
    if (file) {
        CloseHandle(static_cast<HANDLE>(file));
        file = nullptr;
    }
#else
    if (file) {
        ::close(static_cast<int>(reinterpret_cast<std::intptr_t>(file)));
        file = nullptr;
    }
#endif
}

} // namespace

ProjectOwnership::ProjectOwnership(std::filesystem::path marker)
    : marker_(std::move(marker)) {}

ProjectOwnership::State ProjectOwnership::acquire() {
    if (owned_) return State::Clean; // already acquired

    void* created_file = nullptr;
    const bool created = exclusive_create(marker_, created_file);

    if (created) {
        // Brand-new marker: no prior unfinished state. Take the lock and make
        // the marker durable before any derived-state mutation.
        file_ = created_file;
        if (!try_lock(file_)) { close_file(file_); return State::Live; }
        if (!durable_sync(file_)) {
            // fsync unsupported/blocked: proceed without claiming durability
            // (documented power-loss caveat) rather than refusing outright.
        }
        owned_ = true;
        return State::Clean;
    }

    // The marker already exists (either stale, or held by a live build).
    if (!open_existing(marker_, file_)) return State::Failed;
    if (!try_lock(file_)) { close_file(file_); return State::Live; }
    if (!durable_sync(file_)) { /* see above */ }
    owned_ = true;
    return State::Stale;
}

void ProjectOwnership::finish() {
    if (!owned_) return;
    // Remove the marker strictly AFTER the final derived-state mutation, then
    // release the lock. A crash between unlink and close leaves no marker and
    // no further writes (the epoch is complete).
    std::error_code ignored;
    std::filesystem::remove(marker_, ignored);
    close_file(file_);
    owned_ = false;
}

bool ProjectOwnership::live_owner_exists(const std::filesystem::path& marker) {
    void* file = nullptr;
    if (!open_existing(marker, file)) return false; // no marker -> no live owner
    const bool locked = try_lock(file);
    close_file(file);
    return !locked;
}

ProjectOwnership::~ProjectOwnership() {
    // Do NOT remove the marker here: a destructor path after a failed or
    // interrupted epoch must leave `.unfinished` in place so the next build
    // refuses and repair is required. finish() is the only success path.
    close_file(file_);
}
