#pragma once
// Out-of-line operations of the Runtime 2.0 VM (CP11, CP12): the slow paths
// of the dispatch loop (mixed-kind arithmetic, string concatenation,
// comparisons, collection access, binding types) and the run-time errors the
// compiled subset can reach.
//
// CP12: nothing is replayed. Every operation either produces legacy's result
// or stops the run with legacy's own error (message text here; the position
// is the instruction's source offset, chosen by the compiler). Legacy has two
// evaluators with different semantics: the textual (canonical) one and the
// prepared one for loop bodies and the functions called from them. The
// compiler fixes, per instruction, which one legacy would use
// (kInstrPrepared in Instr::flags), so the VM applies exactly that one:
//   * textual: CP09 canonical arithmetic (checked int64 when both operands
//     are integral, "signed 64-bit integer overflow", doubles otherwise);
//   * prepared: legacy's AST evaluator, which computes `+ - * / %` in
//     doubles; a declaration, assignment, compound assignment or increment
//     whose value (or, for compound/increment, an operand) is a "large"
//     number (|n| >= 2^53, or an exact big number) is re-evaluated by legacy
//     on the textual path: the VM runs the statement's textual code instead
//     (StopReason::Reeval, handled inside Vm::run; never a replay).
// Operand kinds the static inference proves impossible are reported as
// internal failures (they would be a compiler defect), never guessed.
#include "rt/atom.h"
#include "rt/opcodes.h"
#include "rt/proto.h"
#include "rt/value.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nift::rt {

struct Fault {
    StopReason reason = StopReason::None;
    std::string message;
};

inline constexpr std::int64_t kSafeInteger = std::int64_t(1) << 53;
inline bool safe_integer(std::int64_t v) { return v > -kSafeInteger && v < kSafeInteger; }
inline bool prepared(const Instr& in) { return (in.flags & kInstrPrepared) != 0; }

bool raise(Fault& fault, StopReason reason, std::string message);
bool internal(Fault& fault, const char* what);

// Legacy's large_num: an exact big number or |n| >= 2^53.
bool is_large(const Value& value);

// Add, Sub, Mul, Div, Mod (`op`) under the evaluator of `in`; string
// concatenation for Add. `out` may alias an operand. With kInstrReeval a
// large operand stops with Reeval before computing.
bool arith_slow(const Instr& in, OpCode op, const Value& left, const Value& right, Value& out, Fault& fault);
// `x += y`: numbers, or two strings (appended in place when unshared).
bool add_assign_slow(const Instr& in, Value& target, const Value& right, Fault& fault);
// Lt, Le, Gt, Ge, Eq, Ne.
bool compare_slow(OpCode op, const Value& left, const Value& right, bool& result, Fault& fault);
bool negate_slow(const Instr& in, const Value& value, Value& out, Fault& fault);
bool positive_slow(const Value& value, Value& out, Fault& fault);

// Legacy binding types (nift_binding_type): null 0, bool 1, int 2 (an
// integral number), double 3, string 4, array 5, object (json) 6, ...
std::int64_t binding_tag(const Value& value);
const char* binding_type_name(std::int64_t tag);
inline std::int64_t fast_tag(const Value& value) { return value.is(Kind::Int) ? 2 : binding_tag(value); }
inline bool tag_assignable(std::int64_t from, std::int64_t to) { return from == to || (from == 2 && to == 3); }
// Legacy nift_binding_type_from_text: a text type of 3 wins, 2 or none defer
// to the value, another known type (a referenced binding's) wins.
inline std::int64_t text_binding_type(std::int64_t text_type, const Value& value) {
    if (text_type == 3) return 3;
    if (text_type >= 0 && text_type != 2) return text_type;
    return fast_tag(value);
}
// The error of a failed binding-type check (`assigned` is the type legacy
// computed for the value; `name` the binding).
bool binding_type_error(const Instr& in, std::int64_t assigned, std::int64_t binding, const Note* note, Fault& fault);

bool get_index(const Instr& in, const Note* note, const Value& object, const Value& key, Value& out, Fault& fault);
bool set_index(const Instr& in, const Note* note, Value& object, const Value& key, const Value& value, Fault& fault);
bool get_member(const Instr& in, const Value& object, Atom key, Value& out, Fault& fault);
bool push_value(Value& array, const Value& value, Fault& fault);
// `length()` (size = false) or `size()` (size = true).
bool length_of(const Instr& in, const Note* note, const Value& value, bool size, Value& out, Fault& fault);

} // namespace nift::rt
