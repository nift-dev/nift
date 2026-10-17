#include "rt/native.h"

#include "rt/render.h"

#include <unordered_set>

// rt-lint: cold-file names are resolved here at compile time only, never on the VM hot path

namespace nift::rt {

bool lookup_native(std::string_view name, NativeInfo& info) {
    if (name == "print") { info = NativeInfo{NativeId::Print, 1}; return true; }
    return false;
}

bool reserved_name(std::string_view name) {
    // Union of the legacy native dispatch heads (ParserExpression.cpp
    // native_heads and call_args), the CLI built-in list, special callable
    // names compared by the evaluator, script invocation bindings and
    // methods compiled here.
    static const std::unordered_set<std::string_view> names = {
        "arch", "args", "async", "attr_escape", "await", "bg", "build", "build_all", "build_names",
        "build_repair", "bytes", "cat", "cd", "close", "cmd", "copy", "cp", "css", "deepcopy", "env",
        "epoch", "err", "error", "exists", "ffi_buffer", "ffi_bytes", "ffi_call", "ffi_callback",
        "ffi_close", "ffi_open", "ffi_sizeof", "ffi_snapshot_bytes", "ffi_struct", "fg", "file", "first",
        "getenv", "hardware_concurrency", "html", "html_escape", "ifstream", "is_dir", "is_file", "jobs",
        "js", "json", "jsx", "len", "length", "ls", "make_dir", "max", "min", "minify", "mkdir",
        "module_path", "move", "mutex", "mv", "ofstream", "open", "open_bytes", "os", "package_path", "page",
        "platform", "print", "project_root", "push", "pwd", "range", "read", "remove", "rm", "run", "same",
        "secure_random_bytes", "setenv", "size", "sleep", "split", "stat", "status", "svg", "thread",
        "timer", "touch", "track", "tracked", "type", "unsetenv", "untrack", "url_encode", "wait", "warn",
        "which", "xml", "atomic", "fn", "struct", "enum", "import", "export", "return", "throw", "try",
        "catch", "if", "else", "while", "for", "break", "continue", "const", "immut", "true", "false",
        "null", "this", "self", "lambda", "content", "input", "script",
    };
    return names.count(name) != 0;
}

bool native_print(const Value& value, std::string& out, std::string& error) {
    if (!append_rendered(value, out)) {
        error = "print: value is not directly renderable";
        return false;
    }
    out.push_back('\n');
    return true;
}

} // namespace nift::rt
