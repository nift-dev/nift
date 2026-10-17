#pragma once
// Legacy static binding-type inference from expression text (CP11), ported
// from Parser::expression_type (src/ParserExpression.cpp).
//
// Legacy gives a binding the type of its declaring expression's *text* where
// it can infer one, not only the type of its value: a spelling with a double
// literal (`1.5`, `1e3`) is double, an arithmetic `+ - * / %` with a double
// operand is double, a bare identifier has that binding's declared type. So
// `v := "" + 1.5` declares a double binding holding the string "1.5", and
// assigning another string to it later fails. The same inference types the
// assigned value of a top-level `x = expr` and a callee's parameters. The VM
// reproduces it: this module evaluates the textual part at compile time; a
// bare identifier becomes a reference to that binding's run-time type.
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace nift::rt {

// Result of the inference: a known type (-1 = none, 2 = int, 3 = double, or
// another binding type), the type of a named binding, or the legacy
// combination of two results.
struct TextType {
    enum class Kind : std::uint8_t { Known, Binding, Meet };
    Kind kind = Kind::Known;
    int known = -1;
    std::string_view binding;  // Kind::Binding
    std::shared_ptr<const TextType> left, right;  // Kind::Meet
};

TextType infer_text_type(std::string_view text);

// True when the result depends on another binding's declared type. Legacy
// loop bodies (and functions called from them) do not propagate a referenced
// binding's type into a declaration (`v2 := v1` with `v1 := 2.0` declares an
// int binding there, a double one at top level), so the VM checks that both
// readings agree (CheckParam after BindType) and bails otherwise.
bool refers_to_binding(const TextType& type);

// Legacy combination of the two sides of `+ - * / %`.
inline int meet_text_types(int left, int right) {
    if (left == 3 || right == 3) return 3;
    if (left == 2 && right == 2) return 2;
    return -1;
}

} // namespace nift::rt
