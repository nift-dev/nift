#pragma once
// Out-of-line operations of the Runtime 2.0 VM (CP11): the slow paths of the
// dispatch loop (mixed-kind arithmetic, string concatenation, comparisons,
// collection access, binding types). Each either produces the legacy result
// or reports a Fault; a fault ends the VM run (the script is then re-run on
// the legacy engine, which reports its own diagnostic: vm.h).
//
// Number range rule: the legacy engine evaluates loop bodies on a separate
// ("prepared") path that computes in doubles and re-evaluates large results
// textually. The two legacy paths agree whenever every integer is below 2^53
// in magnitude and no integer computation yields -0, so the VM faults
// (BailReason::NumberRange) outside that domain instead of choosing a path.
#include "rt/atom.h"
#include "rt/opcodes.h"
#include "rt/proto.h"
#include "rt/value.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nift::rt {

struct Fault {
    BailReason reason = BailReason::None;
    std::string message;
};

inline constexpr std::int64_t kSafeInteger = std::int64_t(1) << 53;
inline bool safe_integer(std::int64_t v) { return v > -kSafeInteger && v < kSafeInteger; }

bool raise(Fault& fault, BailReason reason, const char* message);

// Add, Sub, Mul, Div, Mod with legacy semantics (string concatenation for Add).
// In every function here `out` may alias an operand.
bool arith_slow(OpCode op, const Value& left, const Value& right, Value& out, Fault& fault);
// `x += y`: numbers, or two strings.
bool add_assign_slow(const Value& left, const Value& right, Value& out, Fault& fault);
// Lt, Le, Gt, Ge, Eq, Ne.
// `==`/`!=` on values nested deeper than `depth` raise NestingDepth (review R6).
bool compare_slow(OpCode op, const Value& left, const Value& right, bool& result, std::size_t depth, Fault& fault);
bool negate_slow(const Value& value, Value& out, Fault& fault);
bool positive_slow(const Value& value, Value& out, Fault& fault);

// Legacy binding types (nift_binding_type): null 0, bool 1, int 2 (an
// integral number), double 3, string 4, array 5, object 6, other kinds above.
std::int64_t binding_tag(const Value& value);
inline std::int64_t fast_tag(const Value& value) { return value.is(Kind::Int) ? 2 : binding_tag(value); }
inline bool tag_assignable(std::int64_t from, std::int64_t to) { return from == to || (from == 2 && to == 3); }
// Legacy nift_binding_type_from_text: a text type of 3 wins, 2 or none defer
// to the value, another known type (a referenced binding's) wins.
inline std::int64_t text_binding_type(std::int64_t text_type, const Value& value) {
    if (text_type == 3) return 3;
    if (text_type >= 0 && text_type != 2) return text_type;
    return fast_tag(value);
}
// `x = e`: legacy checks the text-derived type at top level and the value
// type in loop bodies; when the two verdicts differ the VM cannot know which
// path legacy takes, so it bails (Unverified).
bool check_assign(const Value& value, std::int64_t binding_type, std::int64_t text_type, Fault& fault);

bool get_index(const Value& object, const Value& key, Value& out, Fault& fault);
bool set_index(Value& object, const Value& key, const Value& value, Fault& fault);
bool get_member(const Value& object, Atom key, Value& out, Fault& fault);
bool push_value(Value& array, const Value& value, Fault& fault);
// `length()` (size = false) or `size()` (size = true): legacy accepts
// length() on strings and arrays, size() on arrays (and objects: not
// reproduced here); everything else is a legacy error (review D3).
bool length_of(const Value& value, bool size, Value& out, Fault& fault);
bool is_aggregate(const Value& value);

} // namespace nift::rt
