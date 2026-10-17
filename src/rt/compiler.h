#pragma once
// Compiler from the CP10 AST to Runtime 2.0 register bytecode (CP11).
//
// The compilation unit is the whole script (D-001): either every function and
// the top-level program compile, or nothing does and the script runs on the
// legacy engine unchanged. `reason` names the first construct that prevented
// compilation (reported by NIFT_VM_STATS) and `offset` its source position.
#include "rt/ast.h"
#include "rt/proto.h"

#include <cstdint>
#include <memory>
#include <string>

namespace nift::rt {

struct CompileResult {
    std::unique_ptr<Program> program;  // null when the unit must fall back
    std::string reason;
    std::uint32_t offset = 0;
};

// `root` must be the Program node of a successful parse_script.
CompileResult compile_unit(const Ast& ast, NodeId root);

} // namespace nift::rt
