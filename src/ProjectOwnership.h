#pragma once
#include <filesystem>
#include <string>

// Two-layer build ownership protocol (CP2):
//
//   process-held OS lock  = a live process is mutating derived project state
//   .nift/.unfinished     = the previous/current derived-state mutation epoch
//                           was not proven to complete successfully
//
// The OS lock answers "is somebody building right now?" and is released by the
// kernel when the owning process dies. The marker file survives process death
// and answers "did the last build finish?" A live build holds both; a crashed
// build leaves the marker with no lock holder.
//
// A project-local serialization file .nift/.lock serializes the short marker
// create/classify/lock critical section across processes so a freshly created
// marker is never observable unlocked (no timing heuristics). .nift/.lock is
// normal persistent concurrency infrastructure: its presence does not mean a
// command is running and does not require repair, which is deliberately
// distinct from .nift/.unfinished. A legacy .ownership-gate is migrated to
// .lock on first use when it is idle; a live legacy-version process is refused
// (concurrent different-version migration is unsupported).
//
// Acquisition contract (encoded centrally here):
//   Clean  -> the caller created the marker (no prior unfinished state); a
//             normal build may proceed and is the owner.
//   Stale  -> a marker existed and no live process owns the lock; a normal
//             build must refuse (run `build --repair`), while --repair may
//             proceed and takes over ownership.
//   Live   -> a live process owns the lock; the caller must refuse.
//   Failed -> I/O error; the caller must refuse.
//
// No PID ownership, liveness probing, timestamps, UUID schemes or stale-lock
// heuristics are used: the OS lock provides liveness, the marker provides
// crash evidence. There is deliberately no --force escape hatch.
class ProjectOwnership {
public:
    enum class State { Clean, Stale, Live, Failed };

    explicit ProjectOwnership(std::filesystem::path marker);

    // Acquires exclusive ownership (atomic exclusive-create + advisory lock)
    // and durably establishes the marker (one fsync). Returns Clean/Stale on
    // success (the object now owns the lock), Live/Failed on refusal.
    State acquire();

    // Removes the marker and releases the lock. Call ONLY after the final
    // derived-state mutation of a fully successful epoch.
    void finish();

    // Non-owning liveness probe for commands that mutate derived state without
    // running a full build epoch (untrack/rm/cp/mv): returns true when a live
    // process owns the lock, false when no live owner exists (marker may or
    // may not be present). Never creates the marker.
    static bool live_owner_exists(const std::filesystem::path& marker);

    ~ProjectOwnership();

    bool owned() const { return owned_; }

private:
    std::filesystem::path marker_;
    void* file_ = nullptr; // HANDLE (Windows) or an fd stored as intptr (POSIX)
    void* legacy_file_ = nullptr; // idle legacy .ownership-gate handle awaiting migration
    bool owned_ = false;
};
