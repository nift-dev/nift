#pragma once
// Native function table of the Runtime 2.0 VM (CP11): built-ins are bound to
// integer ids at compile time (architecture.md #5); the VM never looks a
// built-in up by name. Also the set of names the legacy engine treats as
// built-ins, which a compiled unit may not rebind (D-009#3: legacy precedence
// between user names and built-ins differs per name, so such units stay on
// the legacy engine until the new precedence rule is implemented).
#include "rt/reserved_names.h"
#include "rt/value.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace nift::rt {

enum class NativeId : std::uint16_t { Print };
// CallNative operand b: a NativeId below kFirstBuiltin, a CP14 built-in
// (natives::BuiltinId = b - kFirstBuiltin, rt/builtin_bind.h) from there on.
inline constexpr std::uint16_t kFirstBuiltin = 1;

struct NativeInfo {
    NativeId id;
    std::uint16_t arity;
};

// Natives callable by name from compiled code. Returns false when `name` is
// not a supported native (the unit then falls back).
bool lookup_native(std::string_view name, NativeInfo& info);

// print(value): appends the legacy rendering and a newline to `out`.
// Returns false (with `error` set) for values legacy refuses to render.
bool native_print(const Value& value, std::string& out, std::string& error);

} // namespace nift::rt
