#pragma once
// CP16 Host layer: dependency-recording hooks (design §2.5). INTERFACE ONLY.
//
// Semantics follow legacy as fixed by feature-host-deps (D-028, integrated
// @2d191e6): every read that influences a build is a dependency whichever API
// performs it ("rw" included), file-backed input streams record at open, and a
// read that fails records the path as absent (missing) or unreadable. HostCall
// calls these hooks per the Recording column of host_op.h: after a successful
// attempt, and failed_read() after a failed read.
//
// Who records is the observer's decision, not HostCall's: HostCall always
// calls the observer it is given. A page render passes the PageObserver;
// hook and standalone contexts pass one whose read() still forwards the bytes
// to the host (legacy c7971365: post-build hooks report what file().open
// read, so a declared dependency changed by a hook is detected); a context
// that records nothing passes none.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace nift::rt::host {

class Observer {
public:
    virtual ~Observer() = default;
    virtual void presence(const std::filesystem::path& path, bool exists, std::string_view kind) = 0;
    virtual void read(const std::filesystem::path& path, std::string_view bytes) = 0;
    virtual void failed_read(const std::filesystem::path& path) = 0;   // absent if missing, else unreadable
    virtual void stream_open(const std::filesystem::path& path) = 0;   // file-backed input stream opened (hashed at open)
    virtual void streamed_read(const std::filesystem::path& path, std::uint64_t size, const std::uint64_t* hash) = 0;
    virtual void metadata_read(const std::filesystem::path& path) = 0;
    virtual void written(const std::filesystem::path& path) = 0;
    virtual void listing(const std::string& key, const std::filesystem::path& path) = 0;
    virtual void environment(const std::string& name, const std::optional<std::string>& value) = 0;
    virtual void environment_snapshot() = 0;
    virtual void platform(const std::string& value) = 0;
};

// What a successful op hands HostCall for recording. Only the fields the op's
// Recording rule reads are used.
struct Observation {
    std::string_view bytes;               // Read / OpenMode("r","rw")
    std::uint64_t size = 0;               // StreamedRead
    const std::uint64_t* hash = nullptr;  // StreamedRead (when snapshot wanted)
    bool exists = false;                  // Presence
    std::string_view kind;                // Presence: "file", "directory", "other"
    bool metadata = false;                // Presence: stat() on an existing file also records metadata_read
    std::string listing_key;              // Listing: "dir:<path>" / "glob:<pattern>"
    std::optional<std::string> env_value; // Environment
    std::string platform;                 // Platform
    std::string_view open_mode;           // OpenMode ("r" and "rw" read; "w" and "a" do not)
};

} // namespace nift::rt::host
