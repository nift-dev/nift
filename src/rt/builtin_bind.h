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
                  natives::kArray == kKindArray && natives::kObject == kKindObject && natives::kAny == kKindAll,
              "CP14 KindSet and CP12 kind bits must agree");
static_assert(static_cast<std::size_t>(natives::BuiltinId::Count) + kFirstBuiltin <= 0x10000,
              "built-in ids fit the CallNative operand");

// The receiver family of `kinds`, or Receiver::None when the kinds are empty,
// mixed or include Unknown.
natives::Receiver receiver_family(std::uint8_t kinds);

enum class MethodBinding : std::uint8_t {
    Bound,        // `spec` is the native
    Dead,         // empty receiver kinds: the call never runs; `spec` is the first native of that name
    NotBuiltin,   // no native of that name for any receiver (`method:<name>`)
    Receiver,     // natives of that name exist, but not for these receiver kinds (W3)
};

// Binds `.name(...)` on a receiver of `kinds`. `length`/`size` without
// arguments on strings and arrays stay the Length opcode (compiler).
MethodBinding bind_method(std::uint8_t kinds, std::string_view name, const natives::BuiltinSpec*& spec);

// A free built-in function (`range`, `min`, `html_escape`, `type`, ...), or null.
const natives::BuiltinSpec* bind_function(std::string_view name);

// True when `name` is a method that only reads its receiver for every
// receiver it is bound on (alias analysis, W5): all CP14 methods.
bool pure_method_name(std::string_view name);

// stringify / prettify / highlight (W6: presentation chains are refused).
bool presentation_builtin(const natives::BuiltinSpec& spec);

} // namespace nift::rt
