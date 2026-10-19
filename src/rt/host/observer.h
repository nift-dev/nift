#pragma once
// CP16 Host layer: dependency-recording hooks (design §2.5). INTERFACE ONLY.
//
// The page-render implementation (PageObserver over the shared
// PageObservation extracted from ParserObservation.cpp) waits until
// feature-host-deps is integrated (design §9 order of work; D-028); standalone
// scripts pass no observer (legacy records nothing there). HostCall calls these
// hooks per the Recording column of host_op.h, after a successful attempt only.
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
    virtual void read_hash_only(const std::filesystem::path& path, std::string_view bytes) = 0;
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
    std::string_view open_mode;           // OpenMode
};

} // namespace nift::rt::host
