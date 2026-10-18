#pragma once
// Out-of-line operations of the Runtime 2.0 VM (CP11, CP12): the slow paths
// of the dispatch loop (mixed-kind arithmetic, string concatenation,
// comparisons, collection access, binding types) and the run-time errors the
// compiled subset can reach.
//
// CP12: nothing is replayed. Every operation either produces its result or
// stops the run with its error (message text here; the position is the
// instruction's source offset, chosen by the compiler). One meaning in every
// context (D-018): CP09 canonical arithmetic (checked int64 when both
// operands are integral, "signed 64-bit integer overflow", exact big numbers,
// doubles otherwise) and canonical messages, whether or not legacy would run
// the code on its prepared (loop) evaluator.
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
    // Set by an arithmetic slow path that met a number legacy calls large
    // (or a result out of range); the VM counts it and clears it (evidence
    // for the D-018 number policy, never a change of behaviour).
    bool large = false;
    // ... and one of its operands was large (the result may be a small number
    // or a boolean that still differs from legacy's double arithmetic).
    bool large_operand = false;
};

inline constexpr std::int64_t kSafeInteger = std::int64_t(1) << 53;
inline bool safe_integer(std::int64_t v) { return v > -kSafeInteger && v < kSafeInteger; }
// Both safe (one compare each, no signed overflow): the fast paths take only
// these, so every large operand reaches a slow path and its evidence (review
// CP12 round 1, D4).
inline bool safe_integers(std::int64_t a, std::int64_t b) {
    constexpr std::uint64_t bias = static_cast<std::uint64_t>(kSafeInteger) - 1;
    return (static_cast<std::uint64_t>(a) + bias <= 2 * bias) & (static_cast<std::uint64_t>(b) + bias <= 2 * bias);
}

bool raise(Fault& fault, StopReason reason, std::string message);
bool internal(Fault& fault, const char* what);

// Legacy's large_num: an exact big number or |n| >= 2^53.
bool is_large(const Value& value);

// Add, Sub, Mul, Div, Mod (`op`); string concatenation for Add. `out` may
// alias an operand.
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
bool binding_type_error(std::int64_t assigned, std::int64_t binding, const Note* note, Fault& fault);

bool get_index(const Note* note, const Value& object, const Value& key, Value& out, Fault& fault);
bool set_index(const Note* note, Value& object, const Value& key, const Value& value, Fault& fault);
bool get_member(const Value& object, Atom key, Value& out, Fault& fault);
bool push_value(Value& array, const Value& value, Fault& fault);
// `length()` (size = false) or `size()` (size = true).
bool length_of(const Note* note, const Value& value, bool size, Value& out, Fault& fault);

} // namespace nift::rt
