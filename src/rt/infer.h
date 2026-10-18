#pragma once
// Static value-kind inference for the Runtime 2.0 compiler (CP12).
//
// The compilation unit is closed: every value of a compiled script comes from
// a literal, an operator, a top-level function or a parameter bound at one of
// the unit's own call sites (no input, no globals of other units). A
// flow-insensitive fixpoint over the whole unit therefore gives, for every
// expression node, a sound over-approximation of the kinds of values it can
// produce at run time (`kinds`), computed with structural types (the element
// type of arrays, the member types of objects by key; depth-capped).
//
// The compiler uses it to refuse, before execution, every operation whose
// legacy behaviour would depend on a run-time kind the VM does not reproduce
// exactly (D-015: refuse instead of modelling): type errors (`1 - "a"`,
// `"a" < 1`, `-"a"`, `s += 1`, `.size()` on a string, member access on a
// number, ...), legacy's prepared rendering of aggregates in concatenation,
// and location references to aggregates bound from places (aliasing). Only
// value-dependent errors with a fixed legacy diagnostic remain run-time
// errors (division by zero, index out of range, integer overflow, ...).
//
// Bindings are resolved with the compiler's lexical rules (block scopes for
// `if`/`while` bodies; a function's parameters and body share one scope).
// Parameters take the union of their arguments at every call site, returns
// the union of their return expressions (plus null when the end of the body
// is reachable). A function no call reaches has empty parameter kinds: its
// operations never execute and impose nothing.
#include "rt/ast.h"
#include "rt/resolver.h"

#include <cstdint>
#include <vector>

namespace nift::rt {

inline constexpr std::uint8_t kKindNull = 1u << 0;
inline constexpr std::uint8_t kKindBool = 1u << 1;
inline constexpr std::uint8_t kKindInt = 1u << 2;   // an integral number
inline constexpr std::uint8_t kKindFrac = 1u << 3;  // a non-integral number
inline constexpr std::uint8_t kKindString = 1u << 4;
inline constexpr std::uint8_t kKindArray = 1u << 5;
inline constexpr std::uint8_t kKindObject = 1u << 6;
inline constexpr std::uint8_t kKindNumber = kKindInt | kKindFrac;
inline constexpr std::uint8_t kKindAggregate = kKindArray | kKindObject;
inline constexpr std::uint8_t kKindScalar = kKindNull | kKindBool | kKindNumber | kKindString;
inline constexpr std::uint8_t kKindAll = 0x7f;
// Derived from something the compiler refuses anyway (an unresolved name, an
// unsupported expression): checks defer to that refusal.
inline constexpr std::uint8_t kKindUnknown = 0x80;

struct UnitKinds {
    std::vector<std::uint8_t> kinds;  // per NodeId (expressions)
    std::uint8_t of(NodeId id) const {
        return id < kinds.size() ? kinds[id] : static_cast<std::uint8_t>(kKindAll | kKindUnknown);
    }
};

UnitKinds infer_unit_kinds(const Ast& ast, NodeId root, const UnitPlan& plan);

} // namespace nift::rt
