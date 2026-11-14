#pragma once
// L6 `native::` (D-042, D-044 #2, D-075 #2, D-095): Option B for exactly seven shell-colliding filesystem functions.
// `native::<name>(...)`, written with nothing around `::` and the `(` straight after the name, calls Nift's own
// native; the VM lexes `native::<name>` as one identifier, so the qualifier is resolved at compile time to the
// existing native id. In VM-default execution the unqualified seven are not built-ins (a user function of that name
// is an ordinary function; without one the program is refused, `native-unqualified`); legacy keeps them, as the
// historical reference. `native` is reserved only as this qualifier: no namespace objects or general namespaces.
// Header-only: the CP13 prevalidator (src/rt/parser-sources.txt) uses it too.
#include <cstdint>
#include <string>
#include <string_view>

namespace nift::rt {

inline bool native_function_name(std::string_view name) {
    return name == "cat" || name == "cp" || name == "mv" ||  // rt-lint: allow-string-compare compile time
           name == "rm" || name == "ls" || name == "mkdir" ||  // rt-lint: allow-string-compare compile time
           name == "touch";  // rt-lint: allow-string-compare compile time
}

constexpr std::string_view kNativeQualifier = "native::";

// "cp" for "native::cp"; empty for an unqualified name.
inline std::string_view native_qualified(std::string_view name) {
    return name.substr(0, kNativeQualifier.size()) == kNativeQualifier ? name.substr(kNativeQualifier.size())
                                                                        : std::string_view();
}

// The diagnostics both engines give for a qualifier they cannot use (same text, at the `native` position).
inline const char* native_not_a_function_suffix() {
    return " is not a native function (native:: names cat, cp, mv, rm, ls, mkdir and touch)";
}
constexpr const char* kNativeMalformed = "malformed native:: call: write native::name(...)";

// The CP13 prevalidator's rule, in both engines before anything runs (empty: no problem). A qualified identifier
// node is allowed only as a call's callee and only for the seven.
inline std::string native_node_problem(std::string_view text, bool callee) {
    const std::string_view qualified = native_qualified(text);
    if (qualified.empty()) return {};
    if (!callee) return kNativeMalformed;
    return native_function_name(qualified) ? std::string() : std::string(text) + native_not_a_function_suffix();
}

// Inside a region the VM parser could not structure: `native` then `:` (any spacing), or a qualified head outside
// the seven. Legacy ignores a trailing comment, so a token inside one (from `begin`, quote-aware) is not checked.
inline std::string native_token_problem(std::string_view src, std::uint32_t begin, std::uint32_t at,
                                        std::uint32_t end, bool colon_next) {
    const std::string_view text = src.substr(at, end - at);
    if (text.substr(0, 6) != "native") return {};  // rt-lint: allow-string-compare cold path
    char quote = 0;
    for (std::uint32_t k = begin; k < at; ++k) {
        const char c = src[k];
        if (quote) {
            if (c == '\\') ++k;
            else if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '/' && k + 1 < at && (src[k + 1] == '/' || src[k + 1] == '*')) {
            return {};
        }
    }
    if (!native_qualified(text).empty()) return native_node_problem(text, true);
    return text == "native" && colon_next ? kNativeMalformed : std::string();  // rt-lint: allow-string-compare cold
}

} // namespace nift::rt
