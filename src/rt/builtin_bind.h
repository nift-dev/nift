#pragma once
// Compile-time binding of CP14 built-ins (rt/natives) for the compiler, the
// kind inference and the alias analysis: one rule, used by all three, so a
// call is typed with the same native it is compiled to (wiring W1-W7,
// docs/runtime-2/checkpoints/cp14-builtins-design.md).
//
// Receiver rule (W3): a method is bound only when the receiver's inferred
// kinds are exactly one family (String, Array, Object, Number = int | frac,
// Bool, Null) and that family has the method, or the method takes any
// receiver (stringify, prettify, highlight) and the kinds hold no Unknown.
// Mixed or Unknown receivers are not bound (the compiler refuses the unit).
#include "rt/infer.h"
#include "rt/native.h"
#include "rt/natives/builtin.h"

#include <cstdint>
#include <string_view>

namespace nift::rt {

static_assert(natives::kNull == kKindNull && natives::kBool == kKindBool && natives::kInt == kKindInt &&
                  natives::kFrac == kKindFrac && natives::kString == kKindString &&
                  natives::kArray == kKindArray && natives::kObject == kKindObject &&
                  (natives::kAny & 0xff) == (kKindAll & 0xff),
              "CP14 KindSet and CP12 kind bits must agree");
// CP15 W8: CP12 now has a distinct Bytes bit, equal to natives::kBytes; the
// old alias with Unknown is gone. Function and Error are outside CP14's
// 8-bit family set and are refused where a native family is required.
static_assert(natives::kBytes == kKindBytes, "the bytes bit must match CP12's Bytes kind");
static_assert(natives::kHandle == kKindHandle && natives::kFile == kKindFile &&
                  natives::kStream == kKindStream,
              "CP17 handle bits must match CP12's kind bits");
static_assert((kKindFunction & natives::kAny) == 0 && (kKindError & natives::kAny) == 0,
              "Function/Error are not CP14 native families");
static_assert(static_cast<std::size_t>(natives::BuiltinId::Count) + kFirstBuiltin <= 0x10000,
              "built-in ids fit the CallNative operand");

// The receiver family of `kinds`, or Receiver::None when the kinds are empty,
// mixed or include Unknown.
natives::Receiver receiver_family(KindSet kinds);

enum class MethodBinding : std::uint8_t {
    Bound,        // `spec` is the native
    Dead,         // empty receiver kinds: the call never runs; `spec` is the first native of that name
    NotBuiltin,   // no native of that name for any receiver (`method:<name>`)
    Receiver,     // natives of that name exist, but not for these receiver kinds (W3)
};

// Binds `.name(...)` on a receiver of `kinds`. `length`/`size` without
// arguments on strings and arrays stay the Length opcode (compiler).
MethodBinding bind_method(KindSet kinds, std::string_view name, const natives::BuiltinSpec*& spec);

// A free built-in function (`range`, `min`, `html_escape`, `type`, ...), or null.
const natives::BuiltinSpec* bind_function(std::string_view name);

// True when `name` is a method that only reads its receiver for every
// receiver it is bound on (alias analysis, W5): all CP14 methods.
bool pure_method_name(std::string_view name);

// stringify / prettify / highlight (W6: presentation chains are refused).
bool presentation_builtin(const natives::BuiltinSpec& spec);

// CP15 W8: a built-in that produces bytes or is a method of bytes (refused,
// `builtin-bytes:<name>`, until CP12's kind set has a bytes bit).
bool bytes_builtin(const natives::BuiltinSpec& spec);
// CP15 W10/W11: `validate`, `copy`, `deepcopy` take only provably plain
// arguments (no Unknown kinds; the VM has no struct/handle values) and no
// quoted-literal argument for `validate` (J-Q2).
bool plain_arguments_only(const natives::BuiltinSpec& spec);

} // namespace nift::rt
