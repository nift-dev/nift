#pragma once
// Non-executing refusal inventory over a private session compile transaction.
#include "rt/compiler.h"
#include "rt/source_map.h"
namespace nift::rt {
std::string session_inventory_json(Ast ast, NodeId root, bool parsed,
                                   const SessionCompileContext& context, const SourceMap& source,
                                   bool expression, std::size_t max_rounds = 2048);
} // namespace nift::rt
