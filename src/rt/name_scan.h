#pragma once
// Slot 3 (owner 2026-11-11 05:41, D-068 #1 and #3; dead-code-admission.md 5.2 "Name certainty"): a call to an
// unknown name is deferred (raised when reached) only when no frame can bind that name at run time. Legacy
// resolves a free call through dynamic scope, so any binding form anywhere in the linked program could supply it.
#include <string_view>

namespace nift::rt {

// True when `name` cannot be bound anywhere in `program` (the whole linked text: the entry and every module).
// The scan is textual and over-approximates every binding form at any depth, taken or not: variables,
// parameters, lambda, catch, `for` and destructuring bindings, fn declarations (nested, conditional, async),
// struct names, struct methods and struct FIELDS (review P1-N), import/export names, and any occurrence in a
// string or comment. Only two kinds of occurrence are not bindings: a member name after `.`, and a plain call
// `name(` that is not a declaration head (`fn(name(`, `fn (name(`, `](name(`).
bool name_unbound(std::string_view program, std::string_view name);

} // namespace nift::rt
