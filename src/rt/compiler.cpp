#include "rt/compiler.h"
#include "rt/session_alias.h"
#include "rt/session_history.h"
#include "rt/session_shape.h"

#include "rt/compiler_handles.h"

#include "rt/builtin_bind.h"
#include "rt/compiler_expr.h"
#include "rt/globals.h"
#include "rt/literal.h"
#include "rt/native.h"
#include "rt/prepared.h"
#include "rt/unit_check.h"

#include <unordered_set>

namespace nift::rt {

namespace detail {

bool UnitCompiler::fail(const Node& node, std::string reason) {
    if (reason_.empty()) {
        reason_ = std::move(reason);
        offset_ = node.begin;
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
        case NodeKind::Assign: ok = assign(node); break;
        case NodeKind::CompoundAssign: ok = compound_assign(node); break;
        case NodeKind::IncDec: ok = inc_dec(node); break;
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

bool UnitCompiler::while_loop(const Node& node) {
    // A `while` always runs on legacy's prepared evaluator (its condition and
    // body), wherever it is; its condition must be one that evaluator accepts.
    ++loop_depth_;
    const bool saved_textual = textual_statement_;
    textual_statement_ = false;
    update_flags();
    Loop loop;
    loop.start = fb_->here();
    loop.try_depth = try_scopes_.size();
    loops_.push_back(loop);
    std::vector<std::size_t> exits;
    bool ok = false_jump(node.a, exits);
    BlockRegs body_regs;
    if (ok) {
        scopes_.push();
        const std::uint16_t mark = fb_->top();
        body_regs = block_begin(node.b);
        ok = statements(ast_.at(node.b));
        block_end(body_regs);
        fb_->release_to(mark);
        scopes_.pop();
    }
    if (ok) {
        // CP17 unwind release: clear the body at each iteration end (`continue` too) and at the exit.
        fb_->set_offset(node.begin);
        const std::size_t iteration_end = fb_->here();
        block_clear(body_regs);
        for (std::size_t at : loops_.back().continues) fb_->patch(at, iteration_end);
        const std::size_t back = fb_->emit(OpCode::Jmp);
        fb_->patch(back, loops_.back().start);
        for (std::size_t at : exits) fb_->patch(at, fb_->here());
        for (std::size_t at : loops_.back().breaks) fb_->patch(at, fb_->here());
        block_clear(body_regs);
    }
    loops_.pop_back();
    if (ok && !statement_preparable(node)) ok = fail(node, "prepared-unpreparable");
    --loop_depth_;
    textual_statement_ = saved_textual;
    update_flags();
    return ok && builder_ok();
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

bool UnitCompiler::loop_jump(const Node& node) {
    if (loops_.empty()) return fail(node, "jump-outside-loop");
    fb_->set_offset(node.begin);
    const std::size_t exited = try_scopes_.size() - loops_.back().try_depth;
    for (std::size_t k = 0; k < exited; ++k) fb_->emit(OpCode::TryEnd, 0, 0, 1);
    const std::size_t at = fb_->emit(OpCode::Jmp);
    if (node.kind == NodeKind::Break) loops_.back().breaks.push_back(at);
    else loops_.back().continues.push_back(at);
    return builder_ok();
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
        result.offset = shape.offset;
        return result;
    }
    const UnitPlan plan = plan_unit(ast, root, limits.session ? &limits.session->phased_functions : nullptr);
    if (!plan.ok) {
        result.reason = plan.reason;
        result.offset = plan.offset;
        return result;
    }
    // Code whose legacy meaning depends on the prepared evaluator is refused
    // before anything runs (rt/prepared.h).
    // The kinds are inferred first (stage 4: outer bindings of a parameter
    // copy); the order of the refusals is unchanged.
    // CP12b: free names of functions that resolve to top-level bindings.
    const GlobalPlan globals = plan_globals(ast, root, plan);
    const UnitKinds kinds = infer_session_aliases(ast, root, plan, limits.kind_walks, &globals, limits.session);
    if (limits.session && kinds.converged) {
        const auto refusal = validate_session_history(kinds, plan.functions.size());
        if (!refusal.empty()) { result.reason = refusal; result.offset = ast.at(root).begin; return result; }
    }
    const PreparedRefusal prepared = check_prepared_contexts(ast, root, plan, kinds.converged ? &kinds : nullptr);
    if (prepared.refused) {
        result.reason = prepared.reason;
        result.offset = prepared.offset;
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
        result.offset = handle_offset;
        return result;
    }
    detail::UnitCompiler compiler(ast, root, plan, kinds, prepared.loop_called, &globals);
    compiler.set_module_names(&limits.module_names);
    compiler.set_session(limits.session);
    if (expression_result) compiler.expression_result();
    auto program = std::make_unique<Program>();
    if (!compiler.compile_program(root, *program)) {
        result.reason = compiler.reason();
        result.offset = compiler.offset();
        if (compiler.unknown_hit() && !limits.session) {
            // Unknown kinds come from something refused anyway: report that
            // refusal when the probe finds one (the unit is refused either way).
            detail::UnitCompiler probe(ast, root, plan, kinds, prepared.loop_called, &globals);
            probe.probe_unknown();
            if (expression_result) probe.expression_result();
            probe.set_module_names(&limits.module_names);
            Program discarded;
            if (!probe.compile_program(root, discarded) && !probe.reason().empty()) {
                result.reason = probe.reason();
                result.offset = probe.offset();
            }
        }
        return result;
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
