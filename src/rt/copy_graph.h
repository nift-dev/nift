#pragma once
// copy() and deepcopy() (D-NEXT-refsem #3; Nift v4.2's contract): the shallow
// and the alias-preserving deep copy of a value within one isolate.
//
//   shallow_copy  a new outer struct instance or collection whose fields,
//                 keys, values, members or elements are the same values
//                 (nested references shared). Arrays and objects are values
//                 until Phase B (copy-on-write): the copy is the value itself,
//                 which no later mutation of either side can tell apart from a
//                 new outer array or object with shared elements (exactly the
//                 shallow copy they get once they are references).
//   deep_copy     a new object for every array, object, struct instance and
//                 collection reachable from the value; two references to one
//                 object in the source are two references to one new object
//                 in the copy (an old-to-new identity map, so a cycle, should
//                 one ever be allowed, would be copied too). Scalars, strings,
//                 errors, callables, handles and futures keep their identity
//                 (a copy never duplicates a resource: D-040 #3).
//
// Both reserve the copy's heap footprint against `heap_limit` (kNoCopyLimit:
// none)
// before allocating anything, so a budget stop leaves nothing half built; a
// failed allocation releases every partial copy (each is owned by a Value).
#include "rt/value.h"

#include <cstddef>

namespace nift::rt {

enum class CopyStatus { Ok, Budget };
inline constexpr std::size_t kNoCopyLimit = static_cast<std::size_t>(-1);

CopyStatus shallow_copy(const Value& source, Value& out, std::size_t heap_limit);
CopyStatus deep_copy(const Value& source, Value& out, std::size_t heap_limit);
// The same as one deep_copy of the tuple `values[0..count)`: references shared
// between the values stay shared between the copies (CP22 cooperative task
// arguments, D-NEXT-refsem #5). `out` receives `count` values.
// `keep_immutable` (B2, owner F3: the cooperative spawn copy, an isolate
// crossing like a thread transfer): each copy of an immutable object is
// immutable too. deepcopy() leaves it false: its copies are mutable.
CopyStatus deep_copy_tuple(const Value* values, std::size_t count, Value* out, std::size_t heap_limit,
                           bool keep_immutable = false);

} // namespace nift::rt
