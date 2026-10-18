#include "rt/compiler.h"

#include "rt/compiler_expr.h"
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

bool UnitCompiler::begin(std::string name, NodeId body) {
    fb_ = std::make_unique<FunctionBuilder>(std::move(name));
    scopes_ = Scopes();
    scopes_.push();
    loops_.clear();
    loop_depth_ = 0;
    textual_statement_ = false;
    return_site_ = if_site_ = index_site_ = kNoSite;
    call_args_ = 0;
    index_depth_ = 0;
    assigned_.clear();
    collect_assigned(ast_, body, assigned_);
    update_flags();
    return true;
}

bool UnitCompiler::finish(std::unique_ptr<Proto>& out) {
    if (!builder_ok()) return false;
    out = fb_->finish();
    return true;
}

bool UnitCompiler::compile_main(NodeId program, std::unique_ptr<Proto>& out) {
    in_function_ = false;
    variant_ = Variant::Textual;
    begin("", program);
    const Node& node = ast_.at(program);
    const NodeId* list = ast_.list(node);
    for (std::uint32_t k = 0; k < node.count; ++k) {
        if (ast_.at(list[k]).kind == NodeKind::FnDecl) continue;  // planned by plan_unit
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
        if (&block == loop_body_) loop_site_ = node.begin;
        if (!statement(list[k])) return false;
    }
    return true;
}

bool UnitCompiler::statement(NodeId id) {
    const Node& node = ast_.at(id);
    const std::uint16_t mark = fb_->top();
    // Inside prepared code, a statement legacy's prepared parser rejects is
    // run by legacy's textual evaluator (StmtKind::Legacy).
    const bool saved_textual = textual_statement_;
    // An `if` condition or `return` expression the prepared parser rejects
    // makes legacy abandon preparing the whole loop body: refused (after the
    // statement's own checks, so their reasons come first).
    bool unpreparable = false;
    if (prepared() && !statement_preparable(node)) {
        if (node.kind == NodeKind::If || node.kind == NodeKind::Return) unpreparable = true;
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
        case NodeKind::Return: ok = return_statement(node); break;
        case NodeKind::Break: case NodeKind::Continue: ok = loop_jump(node); break;
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
    // Textual `if`: legacy reports an error of the condition at the `if`.
    const std::uint32_t saved_if = if_site_;
    if (!prepared()) if_site_ = node.begin;
    const bool cond_ok = false_jump(node.a, to_else);
    if_site_ = saved_if;
    if (!cond_ok) return false;
    const Node& body = ast_.at(node.b);
    scopes_.push();
    const bool ok = statements(body);
    scopes_.pop();
    if (!ok) return false;
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
        if (other_ok && prepared() && !statement_preparable(other)) other_ok = fail(other, "prepared-unpreparable");
    } else {
        scopes_.push();
        const std::uint16_t mark = fb_->top();
        other_ok = statements(other);
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
    loops_.push_back(loop);
    std::vector<std::size_t> exits;
    bool ok = false_jump(node.a, exits);
    if (ok) {
        scopes_.push();
        const std::uint16_t mark = fb_->top();
        const Node* saved_body = loop_body_;
        const std::uint32_t saved_site = loop_site_;
        if (loop_depth_ == 1) loop_body_ = &ast_.at(node.b);
        ok = statements(ast_.at(node.b));
        loop_body_ = saved_body;
        loop_site_ = saved_site;
        fb_->release_to(mark);
        scopes_.pop();
    }
    if (ok) {
        fb_->set_offset(node.begin);
        const std::size_t back = fb_->emit(OpCode::Jmp);
        fb_->patch(back, loops_.back().start);
        for (std::size_t at : exits) fb_->patch(at, fb_->here());
        for (std::size_t at : loops_.back().breaks) fb_->patch(at, fb_->here());
    }
    loops_.pop_back();
    if (ok && !statement_preparable(node)) ok = fail(node, "prepared-unpreparable");
    --loop_depth_;
    textual_statement_ = saved_textual;
    update_flags();
    return ok && builder_ok();
}

bool UnitCompiler::return_statement(const Node& node) {
    if (node.flags & kFlagBare || node.a == kNoNode) {
        fb_->emit(in_function_ ? OpCode::ReturnNull : OpCode::Halt);
        return true;
    }
    if (!no_alias(node.a, node)) return false;
    if (!in_function_ && !kinds_within(node.a, kKindScalar, node, "type:script-return")) return false;
    // Textual `return`: legacy reports errors of the expression at the
    // statement, prefixed "return: " (also for errors in calls made there).
    const std::uint32_t saved = return_site_;
    if (!prepared()) return_site_ = node.begin;
    update_flags();
    std::uint16_t value = 0;
    const bool ok = temp(value) && expr_to(node.a, value);
    if (ok) {
        fb_->set_offset(node.begin);
        fb_->emit(in_function_ ? OpCode::Return : OpCode::TopReturn, value);
    }
    return_site_ = saved;
    update_flags();
    return ok;
}

bool UnitCompiler::loop_jump(const Node& node) {
    if (loops_.empty()) return fail(node, "jump-outside-loop");
    const std::size_t at = fb_->emit(OpCode::Jmp);
    if (node.kind == NodeKind::Break) loops_.back().breaks.push_back(at);
    else fb_->patch(at, loops_.back().start);
    return builder_ok();
}

bool UnitCompiler::expression_statement(const Node& node) {
    const Node& expr = ast_.at(node.a);
    if (expr.kind == NodeKind::Call && ast_.at(expr.a).kind == NodeKind::Member) {
        const Node& callee = ast_.at(expr.a);
        if (text(ast_.at(callee.b)) == "push") {  // rt-lint: allow-string-compare compile time
            const Node& receiver = ast_.at(callee.a);
            if (!plain_postfix(expr) || !plain_postfix(callee) || !no_trailing_comma(expr)) return false;
            std::uint16_t reg = 0, value = 0;
            if (receiver.kind != NodeKind::Ident || !local_register(receiver, reg))
                return fail(receiver, "push-receiver");
            if (expr.count != 1) return fail(node, "static-error:push-arity");
            const NodeId arg = ast_.list(expr)[0];
            if (!kinds_within(callee.a, kKindArray, receiver, "type:push") ||
                !kinds_within(arg, kKindScalar, ast_.at(arg), "aliasing"))
                return false;
            if (!expr_any(arg, value)) return false;
            fb_->set_offset(expr.begin);
            fb_->emit(OpCode::Push, reg, value);
            return true;
        }
    }
    // Legacy renders other expression statements as template text (an
    // aggregate value is an error there); only calls are compiled.
    if (expr.kind != NodeKind::Call) return fail(expr, "expression-statement");
    std::uint16_t scratch = 0;
    if (!temp(scratch)) return false;
    // print(path) on the prepared path: when the location does not exist
    // legacy resolves the statement again on its textual path (its message,
    // its evaluation of the keys): that code is compiled next to it and the
    // lookup continues there (kInstrPathText, StopReason::Reeval).
    const Node& callee = ast_.at(expr.a);
    const bool path_print = prepared() && callee.kind == NodeKind::Ident && expr.count == 1 &&
                            (ast_.at(ast_.list(expr)[0]).kind == NodeKind::Index ||
                             ast_.at(ast_.list(expr)[0]).kind == NodeKind::Member);
    if (!path_print) return expr_to(node.a, scratch);
    const std::size_t first = fb_->here();
    print_statement_ = true;
    const bool ok = expr_to(node.a, scratch);
    print_statement_ = false;
    if (!ok) return false;
    std::size_t lookup = fb_->here();
    for (std::size_t i = first; i < fb_->here(); ++i)
        if (fb_->at(i).flags & kInstrPathText) lookup = i;
    if (lookup == fb_->here()) return expr_to(node.a, scratch);  // not a call of print
    const std::size_t done = fb_->emit(OpCode::Jmp);
    fb_->set_note_alt(fb_->note_index(lookup), fb_->here());
    textual_statement_ = true;
    update_flags();
    const bool alt_ok = expr_to(node.a, scratch);
    textual_statement_ = false;
    update_flags();
    fb_->patch(done, fb_->here());
    fb_->cold_block(done, done + 1, fb_->here());
    return alt_ok && builder_ok();
}

} // namespace detail

CompileResult compile_unit(const Ast& ast, NodeId root) {
    CompileResult result;
    // Every later pass may recurse along the tree: bound its depth first,
    // iteratively (review round 3, F3).
    const UnitShape shape = check_unit_depth(ast, root);
    if (!shape.ok) {
        result.reason = "nesting-limit";
        result.offset = shape.offset;
        return result;
    }
    const UnitPlan plan = plan_unit(ast, root);
    if (!plan.ok) {
        result.reason = plan.reason;
        result.offset = plan.offset;
        return result;
    }
    // Code whose legacy meaning depends on the prepared evaluator is refused
    // before anything runs (rt/prepared.h).
    const PreparedRefusal prepared = check_prepared_contexts(ast, root, plan);
    if (prepared.refused) {
        result.reason = prepared.reason;
        result.offset = prepared.offset;
        return result;
    }
    const UnitKinds kinds = infer_unit_kinds(ast, root, plan);
    detail::UnitCompiler compiler(ast, plan, kinds);
    auto program = std::make_unique<Program>();
    if (!compiler.compile_program(root, *program)) {
        result.reason = compiler.reason();
        result.offset = compiler.offset();
        return result;
    }
    result.program = std::move(program);
    return result;
}

} // namespace nift::rt
