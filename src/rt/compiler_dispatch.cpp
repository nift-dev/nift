// B4 (docs/runtime-2/checkpoints/b4-dispatch.md): a built-in method call on a
// receiver whose kinds are a closed union of plain families (or of map, set
// and stack for their read-only natives) that bind_method does not bind. The
// admission table (rt/op_admission.h) decides; the call compiles to one
// flagged CallNative whose native the VM selects from the receiver's run-time
// family, or legacy's ordinary error for a family without that method.
#include "rt/compiler_dispatch.h"

#include "rt/builtin_bind.h"
#include "rt/op_admission.h"

// rt-lint: cold-file compile-time admission of dynamic method dispatch

namespace nift::rt::detail {

namespace {
void walk_calls(const Ast& ast, const UnitPlan& plan, NodeId id, std::vector<bool>& marked, bool& changed) {
    if (id == kNoNode) return;
    const Node& n = ast.at(id);
    if (n.kind == NodeKind::FnDecl || n.kind == NodeKind::Lambda) return;  // a nested callable's own body
    if (n.kind == NodeKind::Call && ast.at(n.a).kind == NodeKind::Ident) {
        const auto found = plan.index.find(ast.text(ast.at(n.a)));
        if (found != plan.index.end()) {
            const std::size_t f = found->second - 1u;
            if (f < marked.size() && !marked[f] && !(ast.at(plan.functions[f]).flags & kFlagAsync)) {
                marked[f] = true;
                changed = true;
            }
        }
    }
    walk_calls(ast, plan, n.a, marked, changed);
    walk_calls(ast, plan, n.b, marked, changed);
    walk_calls(ast, plan, n.c, marked, changed);
    const NodeId* list = ast.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k) walk_calls(ast, plan, list[k], marked, changed);
}
void seed_loops(const Ast& ast, const UnitPlan& plan, NodeId id, std::vector<bool>& marked, bool& changed) {
    if (id == kNoNode) return;
    const Node& n = ast.at(id);
    if (n.kind == NodeKind::While || n.kind == NodeKind::For) {
        walk_calls(ast, plan, id, marked, changed);
        return;
    }
    seed_loops(ast, plan, n.a, marked, changed);
    seed_loops(ast, plan, n.b, marked, changed);
    seed_loops(ast, plan, n.c, marked, changed);
    const NodeId* list = ast.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k) seed_loops(ast, plan, list[k], marked, changed);
}
} // namespace

// Legacy runs the body of a function called from a `while` or a `for` loop
// (transitively) on its prepared evaluator, where an object's length() counts
// its members; the canonical one rejects it. rt/prepared.h marks only `while`
// callers (its own refusals), so a `for` loop's callee compiled the canonical
// behaviour where legacy printed the count (b4-dispatch.md 2, old vs new).
// Over-approximated: it only refuses more (context:object-length).
bool UnitCompiler::object_length_context() const {
    if (loop_reachable()) return true;
    if (!in_function_ || in_lambda_) return false;
    std::vector<bool> marked(plan_.functions.size(), false);
    bool changed = false;
    seed_loops(ast_, plan_, root_, marked, changed);
    for (std::size_t f = 0; f < plan_.functions.size(); ++f)
        seed_loops(ast_, plan_, ast_.at(plan_.functions[f]).b, marked, changed);
    do {
        changed = false;
        for (std::size_t f = 0; f < marked.size(); ++f)
            if (marked[f]) walk_calls(ast_, plan_, ast_.at(plan_.functions[f]).b, marked, changed);
    } while (changed);
    return function_ < marked.size() && marked[function_];
}

// -1: not a Dynamic case (the caller keeps its refusal); 0: refused here (an
// argument rule); 1: compiled.
int UnitCompiler::dynamic_method_call(const Node& node, NodeId receiver, std::string_view name, std::uint16_t dst) {
    const MethodAdmission admitted = admit_dynamic_method(kinds(receiver), name, node.count);
    if (admitted.admission != Admission::Dynamic) return -1;
    for (const DispatchCandidate& branch : admitted.branches)
        if (branch.spec && !builtin_arguments_ok(node, *branch.spec, receiver)) return 0;
    if (admitted.fails()) {
        // Legacy reports a family without the method before evaluating any
        // argument: arguments must be unobservable (W2/W4's rule), and a
        // malformed-expression report quotes the call's text on one line.
        const NodeId* args = ast_.list(node);
        for (std::uint32_t k = 0; k < node.count; ++k)
            if (!unobservable_argument(args[k])) return fail(node, "builtin-receiver:" + std::string(name)) ? 1 : 0;
        for (const DispatchCandidate& branch : admitted.branches)
            if (branch.failure == DispatchFailure::Malformed && text(node).find('\n') != std::string_view::npos)
                return fail(node, "builtin-receiver:" + std::string(name)) ? 1 : 0;
    }
    return builtin_call(node, *admitted.spec, receiver, dst, true) ? 1 : 0;
}

} // namespace nift::rt::detail
