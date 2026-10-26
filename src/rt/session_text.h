#pragma once
// Resolve retained callable definitions for one fresh shared-compiler unit.
#include "rt/compiler.h"
#include "rt/source_map.h"
#include <unordered_map>
namespace nift::rt {
struct SessionText {
    std::string text;
    std::vector<SourceMap::Piece> pieces;
    std::unordered_map<std::string, std::shared_ptr<const SessionDefinition>> imports;
    std::uint32_t unit_begin = 0;
};
SessionText session_text(const std::string& source, const std::string& path, const SessionCompileContext& context);
} // namespace nift::rt
