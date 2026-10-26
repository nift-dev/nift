#pragma once
// CP13B: immutable source positions for VM Error origins (Phase A C2/Q1).
//
// The map owns every source buffer it reads (interface freeze A1): callers may
// release their buffers after construction. Location tables are built once,
// lazily, on the first `at()` call and safely published to concurrent readers.
// Successful executions never call `at()`, so they pay no source-mapping cost.
#include "rt/module_link.h"
#include "rt/parser.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {

struct SourceOrigin {  // 1-based line/byte-column, as rt::locate
    std::string path;
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    bool operator==(const SourceOrigin& other) const {
        return path == other.path && line == other.line && column == other.column;
    }
    bool operator!=(const SourceOrigin& other) const { return !(*this == other); }
};

class SourceMap {
public:
    struct Piece {
        std::string path;
        std::shared_ptr<const std::string> source;
        std::uint32_t source_begin = 0, length = 0;
    };
    // Immutable virtual source assembled from owned original source ranges.
    static SourceMap pieces(const std::vector<Piece>& pieces);
    // Owns a copy of `source`; `path` is reported unchanged.
    static SourceMap single(std::string path, std::string_view source);
    // Owns copies of the entry and every linked module source. `link` must stay
    // alive and immutable for this map's lifetime (it supplies module lookup);
    // the map itself is safe to share between readers.
    static SourceMap linked(const LinkedUnit& link, std::string entry_path, std::string_view entry_source);

    SourceOrigin at(std::uint32_t linked_offset) const;

    // Test-only instrumentation (compile with -DNIFT_SOURCEMAP_COUNTER). The
    // counter is atomic and never read on the lookup path; production builds
    // have no mutable lookup state at all.
#ifdef NIFT_SOURCEMAP_COUNTER
    std::uint64_t lookup_count() const;
    void reset_lookup_count() const;
#endif

private:
    struct Impl {
        std::vector<std::string> paths;
        std::vector<std::string> sources;
        const LinkedUnit* link = nullptr;  // borrowed for linked(), owned for pieces(); locate() only
        mutable std::once_flag built;
        mutable std::vector<std::vector<std::uint32_t>> line_starts;
        std::unique_ptr<LinkedUnit> owned_link;
#ifdef NIFT_SOURCEMAP_COUNTER
        mutable std::atomic<std::uint64_t> lookups{0};
#endif
    };
    std::shared_ptr<Impl> impl_;
};

} // namespace nift::rt
