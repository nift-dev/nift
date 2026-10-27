#pragma once
// Compiler from the CP10 AST to Runtime 2.0 register bytecode (CP11).
//
// The compilation unit is the whole script (D-001): either every function and
// the top-level program compile, or nothing does and the script runs on the
// legacy engine unchanged. `reason` names the first construct that prevented
// compilation (reported by NIFT_VM_STATS) and `offset` its source position.
#include "rt/ast.h"
#include "rt/proto.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {

struct CompileResult {
    std::unique_ptr<Program> program;  // null when the unit must fall back
    std::string reason;
    std::uint32_t offset = 0;
};

// `root` must be the Program node of a successful parse_script.
// Analysis limits (tests lower them to exercise the refusal).
struct CompileLimits {
    std::size_t kind_walks = 0;  // infer_unit_kinds max_walks (0: automatic)
    // CP12b modules (rt/module_link.h): names declared outside functions in
    // imported modules. A user-call argument path rooted at one is refused
    // (`path-in-call-argument:imported`; records20k's stage 4 lift).
    std::vector<std::string_view> module_names;
    bool linked = false;  // CP22: the unit is a linked module graph (coroutine units refuse it)
    // CP22: compile `thread(f, …)` as a cooperative VM task. Off by default:
    // legacy threads execute in parallel, and until the owner decides the
    // execution model a unit calling thread() is refused (`callable:thread`).
    bool cooperative_threads = false;
    // CP22, non-contractual test switch: admit programs that observe scheduling
    // (record §12.1 A5) so VM-only scheduler tests can run. Off by default.
    bool cooperative_observe = false;
};

CompileResult compile_unit(const Ast& ast, NodeId root, const CompileLimits& limits = CompileLimits());

} // namespace nift::rt
