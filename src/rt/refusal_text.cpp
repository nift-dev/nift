#include "rt/refusal_text.h"

#include <cstddef>
#include <string_view>
#include <vector>

// rt-lint: cold-file user-facing diagnostic text, once per refused unit

namespace nift::rt {

namespace {

// Refusal families by reason prefix (the families of the CP25-R
// classification, tests/runtime2/cp25/classification.tsv). `%` is replaced by
// the text after the first ':' of the reason. First match wins; an exact
// reason is listed before its family prefix.
struct Family { std::string_view prefix; std::string_view text; };
constexpr Family kFamilies[] = {
    {"parse-error", "syntax the VM parser does not accept"},
    {"opaque:template-text", "template text inside a script"},
    {"opaque:", "source the VM parser does not structure (%)"},
    {"callable:", "the call `%()`"},
    {"method:", "the method call `.%()`"},
    {"builtin-bytes:", "bytes values with `%()`"},
    {"builtin-receiver:", "`%()` on this kind of receiver"},
    {"builtin-arity:", "`%()` with this number of arguments"},
    {"type:", "an operation whose operand types the VM cannot prove (%)"},
    {"static-error:", "a statically certain program error the VM does not reproduce yet (%)"},
    {"statement:", "the `%` statement"},
    {"import-link", "this combination of imported modules"},
    {"import-", "this form of import"},
    {"module-host-op:", "the host operation `%()` in a program with imports"},
    {"module-return", "a return statement in an imported module"},
    {"export-struct", "exporting a struct from a module"},
    {"coro-", "this coroutine or async usage"},
    {"interpolation:", "this string interpolation (%)"},
    {"context:", "a context-dependent legacy behaviour (%)"},
    {"struct-", "this struct feature"},
    {"session:", "this statement in an interactive session (%)"},
    {"aliasing", "shared references to arrays or objects (aliasing)"},
    {"alias-argument", "passing an aliased array or object as an argument"},
    {"path-in-call-argument", "an element path as a call argument"},
    {"declare-const", "this use of a const binding"},
    {"declare-pattern", "this declaration pattern"},
    {"assign-free-name", "assignment to a name that is not declared"},
    {"free-name", "a name that is not declared in this scope"},
    {"unresolved-name", "a name the VM cannot resolve"},
    {"fn-name-reserved", "a function named like a built-in"},
    {"param-name-reserved", "a parameter named like a built-in"},
    {"binding-name-reserved", "a binding named like a built-in"},
    {"binding-call-reserved", "calling a binding named like a built-in"},
    {"fn-called-before-declaration", "calling a function before its declaration"},
    {"fn-", "this function declaration form"},
    {"lambda-variadic", "a variadic lambda"},
    {"spread", "the spread operator"},
    {"prepared-", "a legacy prepared-declaration behaviour"},
    {"string-arg-type", "a string literal argument typed by its text"},
    {"index-target", "this index assignment target"},
    {"index-assign-target", "this index assignment target"},
    {"compound-target", "this compound assignment target"},
    {"postfix-spelling", "this postfix operator spelling"},
    {"jump-", "this break or continue placement"},
    {"error-result-expression", "a statement whose result is an Error value"},
    {"expression-statement", "this expression statement"},
    {"shebang-statement", "a shebang line after the first line"},
    {"non-transferable", "a value that cannot cross into the VM"},
    {"host-handle-flow", "this flow of host handles"},
    {"path", "this path expression"},
    {"limit:", "a program beyond the VM compiler's limits (%)"},
    {"analysis-limit:", "a program beyond the VM compiler's analysis limits (%)"},
    {"nesting-limit", "nesting deeper than the VM compiler's limit"},
};

bool plain_shell_word(const std::string& word) {
    if (word.empty()) return false;
    for (const char c : word) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.' || c == '/' || c == '=' || c == ':' || c == ',' ||
                        c == '+' || c == '@' || c == '%';
        if (!ok) return false;
    }
    return true;
}

std::string shell_quote(const std::string& word) {
    if (plain_shell_word(word)) return word;
    std::string out = "'";
    for (const char c : word) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

std::vector<std::string>& invocation() {
    static std::vector<std::string> arguments;
    return arguments;
}

bool& invocation_recorded() {
    static bool recorded = false;
    return recorded;
}

} // namespace

std::string describe_refusal(const std::string& reason) {
    for (const Family& family : kFamilies) {
        if (reason.compare(0, family.prefix.size(), family.prefix) != 0) continue;
        const std::size_t colon = reason.find(':');
        const std::string detail = colon == std::string::npos ? reason : reason.substr(colon + 1);
        std::string out;
        for (const char c : family.text) {
            if (c == '%') out += detail;
            else out += c;
        }
        return out;
    }
    return "a language feature";
}

void set_invocation_arguments(int argc, char** argv) {
    auto& arguments = invocation();
    arguments.clear();
    for (int k = 1; k < argc; ++k) arguments.emplace_back(argv[k] ? argv[k] : "");
    invocation_recorded() = true;
}

std::string legacy_command_hint(const std::string& fallback) {
    std::string out = "NIFT_VM=0 nift";
    if (!invocation_recorded()) return out + ' ' + fallback;
    for (const std::string& argument : invocation()) out += ' ' + shell_quote(argument);
    return out;
}

std::string vm_refusal_diagnostic(const std::string& where, const std::string& reason, const std::string& fallback) {
    return "error: " + where + ": not compiled for the VM (" + reason + "): " + describe_refusal(reason) +
           " is not supported by the Runtime 2.0 VM yet\n"
           "note: the program was not executed: no part of it ran\n"
           "note: to run it on the legacy interpreter: " + legacy_command_hint(fallback) + "\n"
           "note: please report unsupported programs at https://github.com/nift-dev/nift/issues\n";
}

} // namespace nift::rt
