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
    std::unordered_map<std::string, std::string> schema_names;
    std::uint32_t unit_begin = 0;
};
void prepare_session_templates(const Ast&, NodeId, bool, const SessionText&,
                               const SessionCompileContext&, SessionCompileContext&);
void session_structures(const Ast&, NodeId, const SessionText&, const std::string&, SessionCompileContext&);
void session_definitions(const Ast&, NodeId, const SessionText&, const std::string&,
                         const SessionCompileContext&, Program&, std::uint64_t);
SessionText session_text(const std::string& source, const std::string& path, const SessionCompileContext& context);
} // namespace nift::rt
