#pragma once
// CLI-only value presentation, never a callable/resource bridge into the
// legacy evaluator. B4: functions have no shown form (no internal tag), an
// atomic shows its current value.
#include "rt/legacy_bridge.h"

namespace nift::rt {
bool eval_result_to_legacy(const Value& value, nift::RuntimeValue& out, std::string& error);
// B4 (b4-dispatch.md 6): the human-readable session display of a struct
// instance or a map/set/stack at the top level: the descriptor renderer's
// compact form (legacy's `C{a:1}`, `map([])`; private fields skipped; no user
// code runs). False for every other value (its ordinary display applies).
bool session_presented(const Value& value, std::string& text);
} // namespace nift::rt
