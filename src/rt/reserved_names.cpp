#include "rt/reserved_names.h"

#include "rt/legacy_names.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>

// rt-lint: cold-file names are checked at compile and validation time only, never on the VM hot path

// Self-contained (no RuntimeValue): the CP13 prevalidator's binding rule uses
// it too (src/rt/parser-sources.txt).

namespace nift::rt {

bool reserved_name(std::string_view name) {
    // Names extracted from the legacy evaluator source (legacy_names.cpp,
    // generated; review D4/D5), plus the original CP11 hand list: the CLI
    // built-in list, script invocation bindings and methods compiled here.
    if (legacy_special_name(name)) return true;
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

bool legacy_field_call_refused(std::string_view name) {
    static constexpr std::string_view names[] = {  // sorted (binary search); C145 review-f5 F6 table
        "all", "any", "append", "cat", "count", "count_by", "cwd", "done", "elapsed", "env", "filter", "find",
        "find_index", "group_by", "group_by_each", "index_by", "insert", "insert_after", "insert_before", "map",
        "max_by", "min_by", "partition", "pause", "paused", "pipe", "prepend",
#if defined(NIFT_C145_MUTANT) && NIFT_C145_MUTANT == 5  // mutant 5: one of the 10 over-refused names (push) added
        "push",
#endif
        "read_all_bytes", "read_bytes", "read_val", "reduce", "replace_once", "reset", "resume", "run", "running",
        "sort_by", "start", "status", "stderr", "stdin", "stdout", "stop", "unique_by", "write_bytes", "write_val",
    };
#if defined(NIFT_C145_MUTANT) && NIFT_C145_MUTANT == 4  // mutant 4: the F6 guard removed
    return false;
#endif
    return std::binary_search(std::begin(names), std::end(names), name);
}

} // namespace nift::rt
