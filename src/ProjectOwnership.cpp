#include "ProjectOwnership.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>
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
// guarantee ("marker durable before any derived mutation") holds. Returns
// false if the parent cannot be opened or fsynced so the caller can fail
// closed before any .unfinished mutation. On Windows, NTFS journals metadata
// so the creation is durably visible; a directory handle is not opened or
// flushed here, and the function returns true (the documented narrower
// Windows power-loss guarantee).
bool durable_sync_parent(const std::filesystem::path& path) {
#ifdef _WIN32
    (void)path;
    return true;
#else
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty()) return true;
    const int dfd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd < 0) return false;
    const bool ok = ::fsync(dfd) == 0;
    ::close(dfd);
    return ok;
#endif
}

// ---------------------------------------------------------------------------
// Ownership serialization lock (CP15 fix): the marker is created with
// O_CREAT|O_EXCL and then flock-locked in a separate syscall. Without
// serialization, a concurrent process can open and lock the
// freshly-created-but-not-yet-locked marker, classify it Stale and refuse,
// while the creator then fails to lock and refuses too (both refuse; the
// marker remains). To close that window deterministically (no timing
// heuristics), every acquire() first takes a blocking advisory lock on a
// project-local serialization file (.nift/.lock), holds it across the marker
// create+lock critical section, then releases it. A fresh marker is therefore
// never observable unlocked by another process: the creator locks it before
// releasing the serialization lock. The serialization lock is held only across
// the short marker create/classify/lock critical section; the build's
// long-lived ownership is still the non-blocking marker flock, unchanged.
//
// .nift/.lock is normal persistent concurrency infrastructure. Its presence
// does not mean a command is running and does not require repair. That is
// deliberately distinct from .nift/.unfinished, which is evidence that a
// derived-state mutation was not proven to finish and requires
// `nift build --repair`. Newly created .lock files carry that explanation;
// existing files keep their filesystem identity and are never truncated, and
// locking does not depend on the explanatory contents.
// ---------------------------------------------------------------------------

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

constexpr const char* lock_explanation =
    "Nift project lock. This persistent file is normal and does not indicate an active or failed build.\n";

std::filesystem::path lock_path(const std::filesystem::path& marker) {
    return marker.parent_path() / ".lock";
}

// Opens (creating if needed) .lock without truncation, preserving a stable
// filesystem identity. The final component is opened without following a
// symlink/reparse point, and the opened object is validated as a regular file,
// so a path swapped to a symlink between inspection and open is refused rather
// than followed. Returns true on success.
bool open_create_lock(const std::filesystem::path& path, void*& file) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        CloseHandle(h);
        return false;
    }
    file = static_cast<void*>(h);
    return true;
#else
    const int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0644);
    if (fd < 0) return false;
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        return false;
    }
    file = reinterpret_cast<void*>(static_cast<std::intptr_t>(fd));
    return true;
#endif
}

// Legacy migration: the pre-4.0.8 serialization file was .nift/.ownership-gate.
// When it exists, refuse unless no live process holds its advisory lock (an
// idle legacy project may be migrated; a live legacy-version process cannot -
// concurrently running different Nift versions during migration is
// unsupported). On success the idle legacy handle is recorded so the file is
// removed only after .lock is established and locked.
bool open_lock(const std::filesystem::path& marker, void*& file, void*& legacy_file) {
    const std::filesystem::path legacy_path = marker.parent_path() / ".ownership-gate";
    std::error_code ec;
    if (std::filesystem::exists(legacy_path, ec)) {
        if (!open_existing(legacy_path, legacy_file)) return false; // cannot establish state safely
        if (!try_lock(legacy_file)) { // a live process holds the legacy lock
            close_file(legacy_file);
            return false;
        }
    }
    if (!open_create_lock(lock_path(marker), file)) {
        if (legacy_file) close_file(legacy_file);
        return false;
    }
    return true;
}

// Removes the (already verified idle) legacy serialization file now that .lock
// is established and locked. Returns false if the removal fails so the caller
// refuses before any .unfinished mutation while both files remain; the legacy
// file is then retained for diagnosis/retry. Never removes a locked legacy file
// (that decision is made by open_lock before the .lock is established).
bool remove_legacy_serialization(const std::filesystem::path& marker, void*& legacy_file) {
    if (!legacy_file) return true;
    if (std::getenv("NIFT_TEST_LOCK_LEGACY_REMOVE_FAIL")) {
        close_file(legacy_file);
        return false; // simulated removal failure: both files retained
    }
    std::error_code ec;
    const std::filesystem::path legacy_path = marker.parent_path() / ".ownership-gate";
    std::filesystem::remove(legacy_path, ec);
    const bool gone = !std::filesystem::exists(legacy_path, ec);
    close_file(legacy_file);
    return gone;
}

// Writes the explanatory sentence into a freshly created (empty) .lock while
// the caller holds the serialization lock. Writes the complete sentence at
// offset zero with a write-all loop (retrying EINTR on POSIX), verifies the
// final size, durably flushes the file and, where supported, the parent
// directory. Returns false on any failure so the caller refuses before
// creating or classifying .unfinished. Never truncates or rewrites a non-empty
// file; locking does not depend on the contents.
bool populate_lock_if_empty(void* file, const std::filesystem::path& path) {
#ifdef _WIN32
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(static_cast<HANDLE>(file), &size) || size.QuadPart != 0) return true;
    const char* seam = std::getenv("NIFT_TEST_LOCK_WRITE_FAIL");
    const bool seam_error = seam && *seam && std::strcmp(seam, "partial") != 0 &&
                            std::strcmp(seam, "flush") != 0 && std::strcmp(seam, "size") != 0;
    const bool seam_partial = seam && std::strcmp(seam, "partial") == 0;
    const bool seam_flush = seam && std::strcmp(seam, "flush") == 0;
    const bool seam_size = seam && std::strcmp(seam, "size") == 0;
    if (seam_error) return false;
    const std::size_t len = std::strlen(lock_explanation);
    DWORD total = static_cast<DWORD>(seam_partial ? len / 2 : len);
    OVERLAPPED overlapped{};
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(file), lock_explanation, total, &written, &overlapped))
        return false;
    if (written != total) return false;
    if (seam_partial) return false; // simulated partial write, then failure
    if (seam_flush) return false;   // simulated flush failure
    if (!FlushFileBuffers(static_cast<HANDLE>(file))) return false;
    if (seam_size) return false;    // simulated final-size-verification failure
    if (!GetFileSizeEx(static_cast<HANDLE>(file), &size) ||
        size.QuadPart != static_cast<LONGLONG>(len)) return false;
    if (std::getenv("NIFT_TEST_PARENT_SYNC_FAIL")) return false; // simulated parent-sync failure
    if (!durable_sync_parent(path)) return false;
    return true;
#else
    const int fd = static_cast<int>(reinterpret_cast<std::intptr_t>(file));
    struct stat st;
    if (::fstat(fd, &st) != 0 || st.st_size != 0) return true;
    const char* seam = std::getenv("NIFT_TEST_LOCK_WRITE_FAIL");
    const bool seam_error = seam && *seam && std::strcmp(seam, "partial") != 0 &&
                            std::strcmp(seam, "flush") != 0 && std::strcmp(seam, "size") != 0;
    const bool seam_partial = seam && std::strcmp(seam, "partial") == 0;
    const bool seam_flush = seam && std::strcmp(seam, "flush") == 0;
    const bool seam_size = seam && std::strcmp(seam, "size") == 0;
    if (seam_error) return false;
    const std::size_t len = std::strlen(lock_explanation);
    if (::lseek(fd, 0, SEEK_SET) < 0) return false;
    const char* p = lock_explanation;
    std::size_t remaining = seam_partial ? len / 2 : len;
    while (remaining > 0) {
        const ssize_t n = ::write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }
    if (seam_partial) return false; // simulated partial write, then failure
    if (seam_flush) return false;   // simulated flush failure
    if (::fsync(fd) != 0) return false;
    if (seam_size) return false;    // simulated final-size-verification failure
    if (::fstat(fd, &st) != 0 || st.st_size != static_cast<off_t>(len)) return false;
    if (std::getenv("NIFT_TEST_PARENT_SYNC_FAIL")) return false; // simulated parent-sync failure
    if (!durable_sync_parent(path)) return false;
    return true;
#endif
}

// Blocking exclusive lock on .lock (waits for the critical section holder).
bool lock_serial(void* file) {
#ifdef _WIN32
    OVERLAPPED overlapped{};
    return LockFileEx(static_cast<HANDLE>(file), LOCKFILE_EXCLUSIVE_LOCK, 0, 1,
                      0, &overlapped) != 0;
#else
    return ::flock(static_cast<int>(reinterpret_cast<std::intptr_t>(file)),
                   LOCK_EX) == 0;
#endif
}

void unlock_serial(void* file) {
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

} // namespace

bool ProjectOwnership::ensure_lock_file(const std::filesystem::path& project_dir, std::string* error) {
    std::error_code ec;
    const std::filesystem::path lock = project_dir / ".lock";
    const bool exists = std::filesystem::exists(lock, ec);
    if (ec) {
        if (error) *error = "cannot inspect .nift/.lock: " + ec.message();
        return false;
    }
    if (exists) {
        const bool symlink = std::filesystem::is_symlink(lock, ec);
        if (ec) {
            if (error) *error = "cannot inspect .nift/.lock: " + ec.message();
            return false;
        }
        const bool regular = std::filesystem::is_regular_file(lock, ec);
        if (ec) {
            if (error) *error = "cannot inspect .nift/.lock: " + ec.message();
            return false;
        }
        if (symlink || !regular) {
            if (error) *error =
                ".nift/.lock exists but is not a regular file (a symlink or directory); refusing to initialise";
            return false;
        }
    }
    void* file = nullptr;
    if (!open_create_lock(lock, file)) {
        if (error) *error = "cannot open .nift/.lock for writing";
        return false;
    }
    const bool ok = populate_lock_if_empty(file, lock);
    close_file(file);
    if (!ok) {
        if (error) *error = "cannot write .nift/.lock";
        return false;
    }
    return true;
}

ProjectOwnership::ProjectOwnership(std::filesystem::path marker)
    : marker_(std::move(marker)) {}

ProjectOwnership::State ProjectOwnership::acquire() {
    if (owned_) return State::Clean; // already acquired

    // Serialize the marker create+lock critical section across processes so a
    // freshly created marker is never observable unlocked (closes the
    // O_CREAT|O_EXCL / flock race deterministically - see the serialization
    // comment). A legacy .ownership-gate is migrated safely to .lock, or the
    // acquire refuses if a live legacy-version process still holds it.
    void* gate = nullptr;
    if (!open_lock(marker_, gate, legacy_file_)) return State::Failed;
    if (!lock_serial(gate)) {
        close_file(gate);
        if (legacy_file_) close_file(legacy_file_);
        return State::Failed;
    }
    // While holding .lock, populate a freshly created (empty) lock with its
    // explanatory sentence (refusing before any .unfinished mutation if the
    // write cannot be completed) and remove the already-verified-idle legacy
    // file (refusing if the removal fails so both names are never silently
    // present during .unfinished mutation).
    if (!populate_lock_if_empty(gate, lock_path(marker_))) {
        unlock_serial(gate);
        close_file(gate);
        if (legacy_file_) close_file(legacy_file_);
        return State::Failed;
    }
    if (!remove_legacy_serialization(marker_, legacy_file_)) {
        unlock_serial(gate);
        close_file(gate);
        if (legacy_file_) close_file(legacy_file_);
        return State::Failed;
    }

    void* created_file = nullptr;
    const bool created = exclusive_create(marker_, created_file);

    State result = State::Live;
    if (created) {
        // Brand-new marker: no prior unfinished state. While we hold the
        // serialization lock, no other process can observe this marker, so the
        // non-blocking lock below can only fail if a long-lived build holds a
        // marker that we could not have created (impossible: the file did not
        // exist). Take the lock and make the marker durable before any derived
        // mutation.
        file_ = created_file;
        if (try_lock(file_)) {
            durable_sync(file_);
            (void)durable_sync_parent(marker_);
            owned_ = true;
            result = State::Clean;
        } else {
            close_file(file_);
            file_ = nullptr;
            result = State::Live;
        }
    } else {
        // The marker already exists (either stale, or held by a live build).
        // We hold the serialization lock, so its classification is stable: if
        // the flock is free it is genuinely stale (the previous owner crashed
        // or finished); if it is held, a live build owns it.
        if (open_existing(marker_, file_)) {
            if (try_lock(file_)) {
                durable_sync(file_);
                (void)durable_sync_parent(marker_);
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

    unlock_serial(gate);
    close_file(gate);

    // The hold hook blocks until released; it must not run while the
    // serialization lock is held, or a concurrently starting command would
    // block on it instead of observing a Live refusal (the
    // NIFT_TEST_OWNERSHIP_HOLD contract).
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
