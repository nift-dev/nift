#pragma once
// Names the legacy engine may resolve specially (built-in call heads, template
// functions, methods, metadata and reserved bindings, keywords), generated
// from the legacy evaluator source by tools/rt-vm/legacy_names.py.
#include <cstddef>
#include <string_view>

namespace nift::rt {

// True when legacy may dispatch `name` to something other than a user
// definition (review D4/D5): such names are refused at compile time.
bool legacy_special_name(std::string_view name);
std::size_t legacy_special_name_count();
// Legacy reserves these method heads before plain-object callable fields.
bool legacy_special_method(std::string_view name);
// Slot 3: true when `name` is a word in any string literal of the legacy sources, or starts with `__`.
// A call to any other name that is no function, binding or built-in is legacy's `undefined callable`.
bool legacy_known_word(std::string_view name);

} // namespace nift::rt
