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
#include "rt/globals.h"
#include "rt/resolver.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nift::rt {
struct SessionCompileContext;
namespace infer_detail { struct Ty; }

using KindSet = std::uint16_t;
inline constexpr KindSet kKindNull = 1u << 0;
inline constexpr KindSet kKindBool = 1u << 1;
inline constexpr KindSet kKindInt = 1u << 2;   // an integral number
inline constexpr KindSet kKindFrac = 1u << 3;  // a non-integral number
inline constexpr KindSet kKindString = 1u << 4;
inline constexpr KindSet kKindArray = 1u << 5;
inline constexpr KindSet kKindObject = 1u << 6;
inline constexpr KindSet kKindBytes = 1u << 7;     // equal to natives::kBytes (CP15)
#ifdef NIFT_KIND_MUTANT
// Test-only deliberate defect: two new kinds share one bit.
inline constexpr KindSet kKindFunction = kKindBytes;
#else
inline constexpr KindSet kKindFunction = 1u << 8;  // a first-class callable (CP12C)
#endif
inline constexpr KindSet kKindError = 1u << 9;     // reserved for CP13B
inline constexpr KindSet kKindNumber = kKindInt | kKindFrac;
inline constexpr KindSet kKindAggregate = kKindArray | kKindObject;
inline constexpr KindSet kKindScalar = kKindNull | kKindBool | kKindNumber | kKindString;
inline constexpr KindSet kKindAll = kKindNull | kKindBool | kKindInt | kKindFrac | kKindString | kKindArray |
                                   kKindObject | kKindBytes | kKindFunction | kKindError;
// Derived from something the compiler refuses anyway (an unresolved name, an
// unsupported expression). Never permissive: every check treats it as "may be
// anything" and refuses (review CP12 round 1, D1); the unit is refused either
// way, at most for another reason.
inline constexpr KindSet kKindUnknown = 1u << 10;
static_assert(kKindBytes == (1u << 7));
#ifndef NIFT_KIND_MUTANT
static_assert(kKindFunction == (1u << 8));
#endif
static_assert(kKindError == (1u << 9));
static_assert(kKindUnknown == (1u << 10));
static_assert((kKindAll & kKindUnknown) == 0 && (kKindAll & ~(kKindError | (kKindError - 1))) == 0);

struct UnitKinds {
    std::unordered_map<std::string, std::shared_ptr<const infer_detail::Ty>> binding_shapes;
    std::unordered_map<NodeId, std::shared_ptr<const infer_detail::Ty>> session_shapes;
    std::vector<KindSet> kinds;  // per NodeId (expressions)
    // CP12C slice 2: for an iterable expression node, the kinds of the values
    // the default `for` binds (array elements, or object values for the pair
    // form). The compiler does not otherwise keep structural element types.
    std::unordered_map<NodeId, KindSet> elements;
    // False when the fixpoint ran out of its walk budget: the kinds are then
    // not sound and the compiler refuses the unit (`analysis-limit:kinds`),
    // never compiles it with weakened checks (review CP12 round 1, D1).
    std::unordered_map<NodeId, std::vector<NodeId>> callable_targets;
    std::unordered_map<NodeId, bool> callable_unknown;
    std::unordered_set<NodeId> collection_lambdas;
    std::unordered_set<NodeId> collection_objects;
    std::unordered_set<NodeId> collection_arrays;
    std::unordered_map<NodeId,NodeId> bindings;
    std::unordered_map<NodeId,KindSet> collection_elements;
    std::unordered_set<NodeId> collection_text_numbers;
    bool converged = false;
    std::size_t walks = 0;  // unit walks the worklist needed (statistics, tests)
    KindSet of(NodeId id) const {
        return id < kinds.size() ? kinds[id] : static_cast<KindSet>(kKindAll | kKindUnknown);
    }
};

// `max_walks` bounds the worklist (0: 64 walks per unit + 1024); beyond it
// the result is not converged.
// `globals` (CP12b, rt/globals.h): a function's free name that resolves to a
// top-level binding reads that binding's kinds, and its writes widen them for
// every unit (one shared slot); nullptr: free names are unknown, as before.
UnitKinds infer_unit_kinds(const Ast& ast, NodeId root, const UnitPlan& plan, std::size_t max_walks = 0,
                           const GlobalPlan* globals = nullptr, const SessionCompileContext* session = nullptr);

} // namespace nift::rt
