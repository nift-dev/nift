#pragma once
// Compiler from the CP10 AST to Runtime 2.0 register bytecode (CP11).
//
// The compilation unit is the whole script (D-001): either every function and
// the top-level program compile, or nothing does and the script runs on the
// legacy engine unchanged. `reason` names the first construct that prevented
// compilation (reported by NIFT_VM_STATS) and `offset` its source position.
#include "rt/ast.h"
#include "rt/proto.h"
#include "rt/session_definition.h"
#include "rt/session_callable.h"
#include <unordered_set>
#include <unordered_map>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {
namespace infer_detail { struct Ty; }

struct CompileResult {
    std::unique_ptr<Program> program;  // null when the unit must fall back
    std::string reason;
    std::uint32_t offset = 0;
};

// `root` must be the Program node of a successful parse_script.
// Analysis limits (tests lower them to exercise the refusal).
struct SessionSymbol {
    std::string name, struct_name;
    std::shared_ptr<const SessionDefinition> definition;
    std::shared_ptr<const infer_detail::Ty> structure;
    std::shared_ptr<const SessionCallableShape> callable_shape;
    std::uint64_t identity = 0;
    std::uint16_t slot = 0, kinds = 0, returns = 0, arity = 0, contract = 0;
    bool callable = false, const_array = false, defined = true, readonly = false;
    bool untracked_callables = false;
    std::vector<std::uint16_t> params, param_types;
};
struct SessionCompileContext {
    std::vector<SessionSymbol> symbols;
    std::unordered_map<std::string, std::shared_ptr<const SessionDefinition>> structures;
    std::unordered_set<std::string> imported_functions;
    std::unordered_map<std::string, NodeId> declarations;
    std::unordered_map<std::string, NodeId> lambda_templates;
    std::unordered_map<const SessionDefinition*, NodeId> callable_templates;
    std::unordered_map<NodeId, std::shared_ptr<const SessionCallableShape>> capture_callable_seeds;
    std::unordered_map<NodeId, std::shared_ptr<const infer_detail::Ty>> capture_seeds, capture_contracts;
    std::unordered_set<NodeId> const_captures;
    std::unordered_map<NodeId, std::shared_ptr<const SessionDefinition>> template_definitions;
    std::shared_ptr<const std::string> source_owner;
    std::uint16_t slots = 0;
    bool inspect_tail = false;
};
struct CompileLimits {
    SessionCompileContext* session = nullptr;  // private compile transaction, never the live namespace
    std::size_t kind_walks = 0;  // infer_unit_kinds max_walks (0: automatic)
    // CP12b modules (rt/module_link.h): names declared outside functions in
    // imported modules. A user-call argument path rooted at one is refused
    // (`path-in-call-argument:imported`; records20k's stage 4 lift).
    std::vector<std::string_view> module_names;
};

CompileResult compile_unit(const Ast& ast, NodeId root, const CompileLimits& limits = CompileLimits());

// Compile one parsed expression as an owned-result unit. Adds synthetic
// Program/Return nodes to the arena, preserving every source byte offset.
// Execute with VmOptions::return_value; ordinary script returns are unchanged.
// Front ends must enforce their own admissible result/serialization surface.
CompileResult compile_expression_unit(Ast& ast, NodeId expression);
NodeId make_eval_program(Ast& ast, NodeId expression);
CompileResult compile_eval_program(const Ast& ast, NodeId program, const CompileLimits& limits = CompileLimits());

} // namespace nift::rt
