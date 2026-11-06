#include "rt/compiler.h"
#include "rt/reference_probe.h"
#include "rt/session_alias.h"
#include "rt/session_history.h"
#include "rt/session_shape.h"

#include "rt/compiler_coro.h"
#include "rt/reference_guard.h"
#include "rt/compiler_coro_admit.h"
#include "rt/compiler_handles.h"

#include "rt/builtin_bind.h"
#include "rt/compiler_expr.h"
#include "rt/globals.h"
#include "rt/literal.h"
#include "rt/native.h"
#include "rt/prepared.h"
#include "rt/unit_check.h"

#include <unordered_set>

#if defined(NIFT_RT_REFERENCE_PROBE)
extern "C" const char nift_rt_reference_probe_build[] = "B2 reference probe (test configuration)";
#endif

namespace nift::rt {

namespace detail {

bool UnitCompiler::fail(const Node& node, std::string reason) {
    if (reason_.empty()) {
        reason_ = std::move(reason);
        offset_ = node.begin;
        positioned_ = true;
    }
    return false;
}

bool UnitCompiler::builder_ok() {
    if (fb_->error().empty()) return true;
    if (reason_.empty()) reason_ = "limit:" + fb_->error();
    return false;
}

bool UnitCompiler::begin(std::string name, NodeId body, const std::unordered_set<std::string_view>* assigned) {
    fb_ = std::make_unique<FunctionBuilder>(std::move(name));
    scopes_ = Scopes();
    scopes_.push();
    loops_.clear();
    try_scopes_.clear();
    loop_depth_ = 0;
    error_call_offset_ = kNoNode;
    textual_statement_ = false;
    call_args_ = 0;
    index_depth_ = 0;
    assigned_.clear();
    if (assigned) assigned_ = *assigned;  // computed once by the unit analyses
    else collect_assigned(ast_, body, assigned_);
    update_flags();
    return true;
}

bool UnitCompiler::finish(std::unique_ptr<Proto>& out) {
    if (!builder_ok()) return false;
    const auto returns = fb_->session_returns;
    out = fb_->finish();
    out->session_returns = returns;
    return true;
}

bool UnitCompiler::compile_main(NodeId program, std::unique_ptr<Proto>& out) {
    in_function_ = false;
    in_lambda_ = false;
    // collect_assigned over the whole program, reusing the function bodies'
    // sets (a declaration's name and parameters are never assigned).
    std::unordered_set<std::string_view> assigned;
    {
        std::size_t next_fn = 0;
        const Node& root = ast_.at(program);
        const NodeId* items = ast_.list(root);
        for (std::uint32_t k = 0; k < root.count; ++k) {
            if (next_fn < plan_.functions.size() && items[k] == plan_.functions[next_fn]) {
                assigned.insert(assigned_fn_[next_fn].begin(), assigned_fn_[next_fn].end());
                ++next_fn;
            } else {
                collect_assigned(ast_, items[k], assigned);
            }
        }
    }
    begin("", program, &assigned);
    if (!invocation_prologue()) return false;
    const Node& node = ast_.at(program);
    const NodeId* list = ast_.list(node);
    for (std::uint32_t k = 0; k < node.count; ++k) {
        if (ast_.at(list[k]).kind == NodeKind::FnDecl || ast_.at(list[k]).kind == NodeKind::StructDecl)
            { if (session_ && ast_.at(list[k]).kind == NodeKind::FnDecl && !session_function(list[k])) return false;
              continue; }  // planned by plan_unit (CP12b: struct declarations too)
        if (!statement(list[k])) return false;
    }
    fb_->emit(OpCode::Halt);
    fb_->drop_unread_types();
    return finish(out);
}

bool UnitCompiler::statements(const Node& block) {
    const NodeId* list = ast_.list(block);
    for (std::uint32_t k = 0; k < block.count; ++k) {
        // Legacy ignores a `break`/`continue` that is followed by another
        // statement in the same block and runs the rest (review D2, candidate
        // language change D-014): such blocks are not compiled.
        const Node& node = ast_.at(list[k]);
        if ((node.kind == NodeKind::Break || node.kind == NodeKind::Continue) && k + 1 < block.count)
            return fail(node, "jump-not-last");
        if (!statement(list[k])) return false;
    }
    return true;
}

bool UnitCompiler::statement(NodeId id) {
    const Node& node = ast_.at(id);
    const std::uint16_t mark = fb_->top();
    // Inside loop code, a statement legacy's prepared parser rejects is run
    // by legacy's textual evaluator (StmtKind::Legacy): it is not loop code
    // for the context checks. An `if` condition or `return` expression the
    // prepared parser rejects makes legacy abandon preparing the whole loop
    // body: refused (after the statement's own checks, so their reasons come
    // first). Stage 4: in the body of a function called from a loop (outside
    // a loop of its own) legacy abandons preparing the function and runs the
    // call canonically; admitted, its call sites are checked (prepared.cpp).
    const bool saved_textual = textual_statement_;
    bool unpreparable = false;
    if (loop_reachable() && !statement_preparable(node)) {
        if (node.kind == NodeKind::If || node.kind == NodeKind::Return) unpreparable = loop_region();
        else if (node.kind != NodeKind::While) textual_statement_ = true;
    }
    update_flags();
    fb_->set_offset(node.begin);
    bool ok = false;
    switch (node.kind) {
        case NodeKind::Comment: ok = fail(node, "shebang-statement"); break;  // only `#!` statements reach the AST
        case NodeKind::Declare:
            ok = declare(node) && builder_ok();
            textual_statement_ = saved_textual;
            update_flags();
            return ok;  // keeps its binding registers
        case NodeKind::ExprStmt: ok = expression_statement(node); break;
        case NodeKind::Assign: ok = !invocation_write(id) && assign(node); break;
        case NodeKind::CompoundAssign: ok = !invocation_write(id) && compound_assign(node); break;
        case NodeKind::IncDec: ok = !invocation_write(id) && inc_dec(node); break;
        case NodeKind::If: ok = if_chain(node); break;
        case NodeKind::While: ok = while_loop(node); break;
        case NodeKind::For: ok = for_loop(node); break;
        case NodeKind::Return: ok = return_statement(node); break;
        case NodeKind::Break: case NodeKind::Continue: ok = loop_jump(node); break;
        case NodeKind::Try: ok = try_statement(node); break;
        case NodeKind::Throw: ok = throw_statement(node); break;
        default: ok = fail(node, std::string("statement:") + node_kind_name(node.kind)); break;
    }
    if (ok && unpreparable) ok = fail(node, "prepared-unpreparable");
    textual_statement_ = saved_textual;
    update_flags();
    fb_->release_to(mark);
    return ok && builder_ok();
}

bool UnitCompiler::if_chain(const Node& node) {
    std::vector<std::size_t> to_else;
    if (!false_jump(node.a, to_else)) return false;
    const Node& body = ast_.at(node.b);
    scopes_.push();
    BlockRegs body_regs = block_begin(node.b);
    const bool ok = statements(body);
    block_end(body_regs);
    scopes_.pop();
    if (!ok) return false;
    block_clear(body_regs);
    if (node.c == kNoNode) {
        for (std::size_t at : to_else) fb_->patch(at, fb_->here());
        return builder_ok();
    }
    const std::size_t to_end = fb_->emit(OpCode::Jmp);
    for (std::size_t at : to_else) fb_->patch(at, fb_->here());
    const Node& other = ast_.at(node.c);
    bool other_ok = false;
    if (other.kind == NodeKind::If) {
        other_ok = if_chain(other);
        if (other_ok && loop_region() && !statement_preparable(other))
            other_ok = fail(other, "prepared-unpreparable");
    } else {
        scopes_.push();
        const std::uint16_t mark = fb_->top();
        BlockRegs else_regs = block_begin(node.c);
        other_ok = statements(other);
        block_end(else_regs);
        if (other_ok) block_clear(else_regs);
        fb_->release_to(mark);
        scopes_.pop();
    }
    fb_->patch(to_end, fb_->here());
    return other_ok && builder_ok();
}

bool UnitCompiler::return_statement(const Node& node) {
    fb_->session_returns |= node.a == kNoNode ? kKindNull : kinds(node.a);
    const auto end_tries = [this, &node] {
        fb_->set_offset(node.begin);
        for (std::size_t k = 0; k < try_scopes_.size(); ++k) fb_->emit(OpCode::TryEnd, 0, 0, 1);
    };
    if (node.flags & kFlagBare || node.a == kNoNode) {
        end_tries();
        fb_->emit(in_function_ ? OpCode::ReturnNull : OpCode::Halt);
        return true;
    }
    // D-063: the shown result of `nift eval` or of a session expression
    // statement: legacy shows a collection, also inside an array or object
    // (its tag or stringify form); the VM has no form for it: refused before
    // anything runs (M14 drops it).
#if !defined(NIFT_COLLECTIONS_MUTANT) || NIFT_COLLECTIONS_MUTANT != 14
    // B4 (b4-dispatch.md 6): a session shows a struct or map/set/stack value
    // itself (its compact form); one inside an array or object, and every
    // such `nift eval` result, stay refused.
    const bool nested = kinds_.collection_holders.count(node.a) && (kinds(node.a) & kKindAggregate);
    if (expression_result_ && !in_function_ && unit_makes_collections() &&
        (session_ ? nested : ((kinds(node.a) & kKindTagged) || kinds_.collection_holders.count(node.a))))
        return fail(ast_.at(node.a), session_ ? "session:collection-result" : "eval:collection-result");
#endif
    if ((!expression_result_ || in_function_) && !no_alias(node.a, node)) return false;
    if (!in_function_ && !expression_result_ &&
        !kinds_within(node.a, kKindScalar, node, "type:script-return")) return false;
    // Other expression errors retain D-009#5 / D-018 attribution. Missing
    // selector reads in a lambda preserve the legacy return statement site.
    std::uint16_t value = 0;
    const std::uint8_t saved_flags = fb_->flags();
#if defined(NIFT_CP19_EVAL_MUTANT_RETURN) // test-only: expose synthetic return again
    const bool real_return = true;
#else
    const bool real_return = !expression_result_ || in_function_;
#endif
    fb_->set_flags(static_cast<std::uint8_t>(saved_flags | (real_return ? kInstrReturnCall : 0)));
    const std::uint32_t saved_return_offset = return_read_offset_;
    if (in_lambda_) return_read_offset_ = node.begin;
    const bool value_ok = temp(value) && expr_to(node.a, value);
    return_read_offset_ = saved_return_offset;
    fb_->set_flags(saved_flags);
    if (!value_ok) return false;
    end_tries();
    fb_->set_offset(node.begin);
    fb_->emit(in_function_ ? OpCode::Return : OpCode::TopReturn, value);
    return true;
}

} // namespace detail

static CompileResult compile_unit_impl(const Ast& ast, NodeId root, const CompileLimits& limits,
                                       bool expression_result) {
    CompileResult result;
    // Every later pass may recurse along the tree: bound its depth first,
    // iteratively (review round 3, F3).
    const UnitShape shape = check_unit_depth(ast, root);
    if (!shape.ok) {
        result.reason = "nesting-limit";
        result.offset = shape.offset; result.positioned = true;
        return result;
    }
    const UnitPlan plan = plan_unit(ast, root, limits.session ? &limits.session->phased_functions : nullptr);
    if (!plan.ok) {
        result.reason = plan.reason;
        result.offset = plan.offset; result.positioned = true;
        return result;
    }
    // Code whose legacy meaning depends on the prepared evaluator is refused
    // before anything runs (rt/prepared.h).
    // The kinds are inferred first (stage 4: outer bindings of a parameter
    // copy); the order of the refusals is unchanged.
    // CP12b: free names of functions that resolve to top-level bindings.
    const GlobalPlan globals = plan_globals(ast, root, plan);
    const UnitKinds kinds = infer_session_aliases(ast, root, plan, &globals, limits);
    if (limits.session && kinds.converged) {
        const auto refusal = validate_session_history(kinds, plan.functions.size());
        if (!refusal.empty()) { result.reason = refusal; result.offset = ast.at(root).begin; return result; }
    }
    const PreparedRefusal prepared = check_prepared_contexts(ast, root, plan, kinds.converged ? &kinds : nullptr);
    if (prepared.refused) {
        result.reason = prepared.reason;
        result.offset = prepared.offset; result.positioned = true;
        return result;
    }
    if (!kinds.converged) {  // never compile with unsound kinds (review D1)
        result.reason = "analysis-limit:kinds";
        result.offset = ast.at(root).begin;
        return result;
    }
    std::string handle_reason;
    std::uint32_t handle_offset = 0;
    if (detail::check_handle_flow(ast, root, kinds, handle_reason, handle_offset, limits.session)) {
        result.reason = std::move(handle_reason);
        result.offset = handle_offset; result.positioned = true;
        return result;
    }
    // Persistent task/global snapshots require CP22's separate lifecycle
    // reconciliation. Refuse atomically rather than run coroutine opcodes
    // without the bootstrap VM's scheduler or share another task's cells.
    if (limits.session && plan.coroutines) {
        result.reason = "session:coroutine";
        result.offset = ast.at(root).begin;
        return result;
    }
    if (!check_coroutine_modules(ast, root, plan, kinds, limits, result)) return result;  // imports + tasks
    // CP22: a coroutine unit's futures and excluded constructs (compiler_coro.h).
    if (!check_coroutine_unit(ast, root, plan, kinds, limits, result.reason, result.offset, result.positioned))
        return result;  // positioned only where the check recorded a node's offset (not coro-module)
    detail::UnitCompiler compiler(ast, root, plan, kinds, prepared.loop_called, &globals);
    compiler.set_module_names(&limits.module_names);
    compiler.set_session(limits.session, limits.invocation);
    if (expression_result) compiler.expression_result();
    compiler.set_parallel_tasks(limits.parallel_tasks);
    auto program = std::make_unique<Program>();
    if (!compiler.compile_program(root, *program)) {
        result.reason = compiler.reason();
        result.offset = compiler.offset(); result.positioned = compiler.positioned();
        if (compiler.unknown_hit() && !limits.session) {
            // Unknown kinds come from something refused anyway: report that
            // refusal when the probe finds one (the unit is refused either way).
            detail::UnitCompiler probe(ast, root, plan, kinds, prepared.loop_called, &globals);
            probe.probe_unknown();
            if (expression_result) probe.expression_result();
            probe.set_module_names(&limits.module_names);
            probe.set_parallel_tasks(limits.parallel_tasks);
            probe.set_session(nullptr, limits.invocation);
            Program discarded;
            if (!probe.compile_program(root, discarded) && !probe.reason().empty()) {
                result.reason = probe.reason();
                result.offset = probe.offset(); result.positioned = probe.positioned();
            }
        }
        return result;
    }
    // D-NEXT-refsem: the Phase A forward-compatibility guard (reference_guard.h),
    // on a unit the compiler otherwise admits (earlier refusals keep their reason).
#if defined(NIFT_RT_REFERENCE_PROBE)  // B2 test configuration only (rt/reference_probe.h)
    if (false &&
#else
    if (
#endif
        !check_reference_guard(ast, root, plan, kinds, limits.session != nullptr, result.reason, result.offset,
                               limits.session ? &limits.session->capture_initializers : nullptr)) {
        result.positioned = true;
        return result;
    }
    if (plan.coroutines) {  // CP22: the scheduler, and the post-compile coroutine checks
        program->has_coroutines = true;
        // Both checks report whether the refused instruction has a recorded source offset.
        if (!check_coroutine_program(*program, result.reason, result.offset, result.positioned,
                                     limits.parallel_tasks))
            return result;
        if (!limits.parallel_tasks &&
            !check_cooperative_admission(*program, limits.cooperative_observe, result.reason, result.offset,
                                         result.positioned))
            return result;
        result.positioned = false;
    }
    if (limits.session) {  // B5 (U6): monotone over published statements
        const auto join = [](std::shared_ptr<const infer_detail::Ty>& into,
                             const std::shared_ptr<const infer_detail::Ty>& add) {
            if (add) into = into ? infer_detail::unite(into, add) : add;
        };
        join(limits.session->heap, kinds.session_heap);
        join(limits.session->heap_objects, kinds.session_heap_objects);
    }
    if (limits.session)  // B5 (U6): every binding is private only as this statement proved it
        for (auto& symbol : limits.session->symbols) {
            const auto found = kinds.session_private.find(symbol.name);
            symbol.heap_contents = found == kinds.session_private.end() ? nullptr : found->second;
        }
    if (limits.session && limits.session->coherent_recompile)
        for (auto& symbol : limits.session->symbols) {
            const auto shape = kinds.binding_shapes.find(symbol.name);
            if (shape != kinds.binding_shapes.end()) symbol.structure = session_shape(shape->second);
        }
    if (limits.session) record_session_history(ast, kinds, *limits.session, *program);
    result.program = std::move(program);
    return result;
}

CompileResult compile_unit(const Ast& ast, NodeId root, const CompileLimits& limits) {
    return compile_unit_impl(ast, root, limits, false);
}
CompileResult compile_eval_program(const Ast& ast, NodeId program, const CompileLimits& limits) {
    return compile_unit_impl(ast, program, limits, true);
}
} // namespace nift::rt
