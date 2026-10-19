#pragma once
// Declared-name heuristics of the refusal inventory (refusal_inventory.h):
// the names a statement's source text declares, for statements the AST does
// not structure (Opaque, export). Test tooling only.
#include "rt/ast.h"

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace nift::rt::inventory {

// `fn (name(`, `@fn(name(`, `struct (name)`, `enum name {`,
// `[const|immut] name :=` (optionally after `export`).
std::vector<std::string_view> declared_names_in_text(std::string_view text);

// The span of an identifier of the source that the compiler accepts as a
// binding name (not reserved, not a legacy name, not a top-level function's
// name), for synthetic declarations; {0, 0} if there is none.
std::pair<std::uint32_t, std::uint32_t> spill_name(const Ast& ast, NodeId root);

} // namespace nift::rt::inventory
