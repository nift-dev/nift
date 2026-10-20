#pragma once
// CP16: THE dependency recorder of the Host layer (design §2.5, D-028 as
// fixed by feature-host-deps). One implementation for every engine: the
// legacy interpreter (ParserHost.cpp) and, when its host natives land, the
// VM construct a PageObserver over the execution's RenderHost and
// RenderResult and hand it to their HostCalls. Recording therefore cannot
// differ by engine; only the decision of *which* context observes is the
// engine's, and it is passed in as `Mode` (computed the same way by both).
//
//   observing:  a page render or build script (and their async/thread
//               workers): reads, presence, listings and environment values
//               are recorded into the RenderResult.
//   standalone: a standalone script or the shell (env()/platform() keep
//               legacy's own conditions, see below).
//
// Hook and standalone contexts do not observe, but file().open reads there
// still report their bytes to the host (c7971365, open_read()).
#include "RenderHost.h"
#include "Types.h"
#include "rt/host/observer.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

class PageObserver : public nift::rt::host::Observer {
public:
    struct Mode {
        bool observing = false;
        bool standalone = true;
    };
    PageObserver(const RenderHost& host, RenderResult& result, Mode mode) : host_(host), result_(result), mode_(mode) {}

    // nift::rt::host::Observer
    void presence(const std::filesystem::path& path, bool exists, std::string_view kind) override;
    void read(const std::filesystem::path& path, std::string_view bytes) override;
    void open_read(const std::filesystem::path& path, std::string_view bytes) override;
    void failed_read(const std::filesystem::path& path) override;
    void stream_open(const std::filesystem::path& path) override;
    void streamed_read(const std::filesystem::path& path, std::uint64_t size, const std::uint64_t* hash) override;
    void metadata_read(const std::filesystem::path& path) override;
    void written(const std::filesystem::path& path) override;
    void listing(const std::string& key, const std::filesystem::path& path) override;
    void environment(const std::string& name, const std::optional<std::string>& value) override;
    void environment_snapshot() override;
    void platform(const std::string& value) override;

    // True when a read must produce a dependency snapshot (hash/hybrid page
    // render); lets streaming readers skip computing it otherwise.
    bool snapshot_wanted() const { return mode_.observing && host_.observe_dependencies; }

private:
    void record_hash(const std::filesystem::path& path, std::string_view bytes);
    const RenderHost& host_;
    RenderResult& result_;
    Mode mode_;
};
