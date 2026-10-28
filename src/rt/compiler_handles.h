#pragma once
// CP17 W13: handle-flow admission for the Runtime 2.0 compiler.
#include "rt/compiler_expr.h"

#include <cstdint>
#include <string>

namespace nift::rt::detail {

// True when a handle value appears outside the W13 positions; sets the refusal
// reason and source offset.
bool check_handle_flow(const Ast& ast, NodeId root, const UnitKinds& kinds, std::string& reason,
                       std::uint32_t& offset, const SessionCompileContext* session = nullptr);

// The argument node of a file/stream write that is (or contains) an
// interpolated string literal, or kNoNode. Legacy writes such a literal's raw
// text; the VM must fall back rather than write different bytes.
NodeId write_literal_argument(const Ast& ast, const Node& call, const natives::BuiltinSpec& spec);

} // namespace nift::rt::detail
