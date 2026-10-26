#include "rt/source_map.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <unordered_map>

namespace nift::rt {

namespace {

std::vector<std::uint32_t> line_starts(std::string_view source) {
    std::vector<std::uint32_t> starts{0};
    for (std::uint32_t i = 0; i < source.size(); ++i)
        if (source[i] == '\n') starts.push_back(i + 1u);
    return starts;
}

} // namespace

SourceMap SourceMap::single(std::string path, std::string_view source) {
    SourceMap map;
    map.impl_ = std::make_shared<Impl>();
    map.impl_->paths.push_back(std::move(path));
    map.impl_->sources.emplace_back(source);
    return map;
}

SourceMap SourceMap::linked(const LinkedUnit& link, std::string entry_path, std::string_view entry_source) {
    SourceMap map;
    map.impl_ = std::make_shared<Impl>();
    map.impl_->link = &link;
    map.impl_->paths.push_back(std::move(entry_path));
    map.impl_->sources.emplace_back(entry_source);
    for (std::size_t m = 1; m < link.graph.modules.size(); ++m) {
        const Module& module = *link.graph.modules[m];
        map.impl_->paths.push_back(module.path.generic_string());
        map.impl_->sources.push_back(module.source);
    }
    return map;
}

SourceMap SourceMap::pieces(const std::vector<Piece>& pieces) {
    SourceMap map;
    map.impl_ = std::make_shared<Impl>();
    map.impl_->owned_link = std::make_unique<LinkedUnit>();
    map.impl_->link = map.impl_->owned_link.get();
    std::unordered_map<const std::string*, std::size_t> modules;
    std::uint32_t position = 0;
    for (const auto& piece : pieces) {
        if (!piece.source || piece.source_begin > piece.source->size())
            throw std::invalid_argument("invalid source map piece");
        if (piece.length > 0xffffffffu - position) throw std::length_error("source map offset limit");
        const auto inserted = modules.emplace(piece.source.get(), modules.size());
        const auto module = inserted.first->second;
        if (inserted.second) {
            map.impl_->paths.push_back(piece.path);
            map.impl_->sources.push_back(*piece.source);
        }
        map.impl_->owned_link->segments.push_back(
            LinkSegment{position, position + piece.length, module, piece.source_begin});
        position += piece.length;
    }
    return map;
}

#ifdef NIFT_SOURCEMAP_COUNTER
std::uint64_t SourceMap::lookup_count() const {
    return impl_ ? impl_->lookups.load(std::memory_order_relaxed) : 0;
}
void SourceMap::reset_lookup_count() const {
    if (impl_) impl_->lookups.store(0, std::memory_order_relaxed);
}
#endif

SourceOrigin SourceMap::at(std::uint32_t linked_offset) const {
#ifdef NIFT_SOURCEMAP_COUNTER
    impl_->lookups.fetch_add(1, std::memory_order_relaxed);
#endif
    if (!impl_ || impl_->sources.empty()) throw std::logic_error("SourceMap used after move");
    std::call_once(impl_->built, [this] {
        // Build in local storage: a throwing allocation leaves the shared
        // tables untouched, so a retry starts from an empty, complete build.
        std::vector<std::vector<std::uint32_t>> tables;
        tables.reserve(impl_->sources.size());
        for (const std::string& source : impl_->sources) tables.push_back(line_starts(source));
        impl_->line_starts = std::move(tables);
    });
    std::size_t module = 0;
    std::uint32_t offset = linked_offset;
    if (impl_->link != nullptr) {
        const LinkedUnit::Location where = impl_->link->locate(linked_offset);
        module = where.module;
        offset = static_cast<std::uint32_t>(where.offset);
    }
    if (module >= impl_->sources.size()) throw std::logic_error("SourceMap module out of range");
    const std::string& source = impl_->sources[module];
    offset = std::min<std::uint32_t>(offset, static_cast<std::uint32_t>(source.size()));
    const std::vector<std::uint32_t>& starts = impl_->line_starts[module];
    const auto it = std::upper_bound(starts.begin(), starts.end(), offset);
    const std::size_t line = static_cast<std::size_t>(it - starts.begin());
    SourceOrigin origin;
    origin.path = impl_->paths[module];
    origin.line = static_cast<std::uint32_t>(line);
    origin.column = offset - starts[line - 1] + 1u;
    return origin;
}

} // namespace nift::rt
