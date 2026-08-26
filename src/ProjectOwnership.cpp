#include "ProjectOwnership.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <thread>

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

// On POSIX, syncing the newly created marker file flushes its contents and
// inode but NOT the parent-directory entry that links the name to the inode.
// A power loss can persist the file without the directory entry (or vice
// versa). Flushing the parent directory closes that window so the stated
// guarantee ("marker durable before any derived mutation") holds. This is one
// extra fsync per build, not per output.
void durable_sync_parent(const std::filesystem::path& path) {
#ifdef _WIN32
    // NTFS journals metadata so the creation is durably visible; a directory
    // handle is not opened/flushed here. The documented Windows power-loss
    // guarantee is therefore "file data + metadata flushed via
    // FlushFileBuffers; creation is journaled by NTFS" - narrower than the
    // POSIX parent-directory fsync, and stated precisely (see CP2.1 report).
    (void)path;
#else
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty()) return;
    const int dfd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd < 0) return;
    ::fsync(dfd);
    ::close(dfd);
#endif
}

// ---------------------------------------------------------------------------
// Ownership gate (CP15 fix): the marker is created with O_CREAT|O_EXCL and
// then flock-locked in a separate syscall. Without serialization, a concurrent
// process can open and lock the freshly-created-but-not-yet-locked marker,
// classify it Stale and refuse, while the creator then fails to lock and
// refuses too (both refuse; the marker remains). To close that window
// deterministically (no timing heuristics), every acquire() first takes a
// blocking advisory lock on a project-local gate file
// (.nift/.ownership-gate), holds it across the marker create+lock critical
// section, then releases it. A fresh marker is therefore never observable
// unlocked by another process: the creator locks it before releasing the gate.
// The gate hold time is microseconds; the build's long-lived ownership is
// still the non-blocking marker flock, unchanged.
// ---------------------------------------------------------------------------

std::filesystem::path gate_path(const std::filesystem::path& marker) {
    return marker.parent_path() / ".ownership-gate";
}

// Opens (creating if needed) the gate file. Returns true on success.
bool open_gate(const std::filesystem::path& path, void*& file) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    file = static_cast<void*>(h);
    return true;
#else
    const int fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) return false;
    file = reinterpret_cast<void*>(static_cast<std::intptr_t>(fd));
    return true;
#endif
}

// Blocking exclusive lock on the gate (waits for the critical section holder).
bool lock_gate(void* file) {
#ifdef _WIN32
    OVERLAPPED overlapped{};
    return LockFileEx(static_cast<HANDLE>(file), LOCKFILE_EXCLUSIVE_LOCK, 0, 1,
                      0, &overlapped) != 0;
#else
    return ::flock(static_cast<int>(reinterpret_cast<std::intptr_t>(file)),
                   LOCK_EX) == 0;
#endif
}

void unlock_gate(void* file) {
#ifdef _WIN32
    OVERLAPPED overlapped{};
    UnlockFileEx(static_cast<HANDLE>(file), 0, 1, 0, &overlapped);
#else
    ::flock(static_cast<int>(reinterpret_cast<std::intptr_t>(file)), LOCK_UN);
#endif
}

// Test-only synchronization hook (sanctioned by the CP2.1 review). When
// NIFT_TEST_OWNERSHIP_HOLD=<dir> is set, a successfully acquired owner writes
// <dir>/acquired and then blocks until <dir>/release appears, so concurrency
// tests can deterministically hold a real mutator's ownership while a second
// command is exercised. Never active in normal use.
static void test_hold_after_acquire() {
    const char* hold = std::getenv("NIFT_TEST_OWNERSHIP_HOLD");
    if (!hold || !*hold) return;
    const std::filesystem::path dir(hold);
    std::error_code ignored;
    { std::ofstream(dir / "acquired"); }
    while (!std::filesystem::exists(dir / "release", ignored)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::filesystem::remove(dir / "acquired", ignored);
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

    // Serialize the marker create+lock critical section across processes so a
    // freshly created marker is never observable unlocked (closes the
    // O_CREAT|O_EXCL / flock race deterministically - see the gate comment).
    void* gate = nullptr;
    if (!open_gate(gate_path(marker_), gate)) return State::Failed;
    if (!lock_gate(gate)) {
        close_file(gate);
        return State::Failed;
    }

    void* created_file = nullptr;
    const bool created = exclusive_create(marker_, created_file);

    State result = State::Live;
    if (created) {
        // Brand-new marker: no prior unfinished state. While we hold the gate,
        // no other process can observe this marker, so the non-blocking lock
        // below can only fail if a long-lived build holds a marker that we
        // could not have created (impossible: the file did not exist). Take
        // the lock and make the marker durable before any derived mutation.
        file_ = created_file;
        if (try_lock(file_)) {
            durable_sync(file_);
            durable_sync_parent(marker_);
            owned_ = true;
            result = State::Clean;
        } else {
            close_file(file_);
            file_ = nullptr;
            result = State::Live;
        }
    } else {
        // The marker already exists (either stale, or held by a live build).
        // We hold the gate, so its classification is stable: if the flock is
        // free it is genuinely stale (the previous owner crashed or finished);
        // if it is held, a live build owns it.
        if (open_existing(marker_, file_)) {
            if (try_lock(file_)) {
                durable_sync(file_);
                durable_sync_parent(marker_);
                owned_ = true;
                result = State::Stale;
            } else {
                close_file(file_);
                file_ = nullptr;
                result = State::Live;
            }
        } else {
            result = State::Failed;
        }
    }

    unlock_gate(gate);
    close_file(gate);

    // The hold hook blocks until released; it must not run while the gate is
    // held, or a concurrently starting command would block on the gate instead
    // of observing a Live refusal (the NIFT_TEST_OWNERSHIP_HOLD contract).
    if (result == State::Clean || result == State::Stale) {
        test_hold_after_acquire();
    }
    return result;
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
