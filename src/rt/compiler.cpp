#include "rt/compiler.h"

#include "rt/compiler_expr.h"
#include "rt/literal.h"
#include "rt/native.h"
#include "rt/prepared.h"

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
    assigned_.clear();
    collect_assigned(ast_, body, assigned_);
    return true;
}

bool UnitCompiler::finish(std::unique_ptr<Proto>& out) {
    if (!builder_ok()) return false;
    out = fb_->finish();
    return true;
}

bool UnitCompiler::compile_main(NodeId program, std::unique_ptr<Proto>& out) {
    in_function_ = false;
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

bool UnitCompiler::compile_function(NodeId fn, std::unique_ptr<Proto>& out) {
    in_function_ = true;
    const Node& node = ast_.at(fn);
    begin(std::string(text(ast_.at(node.a))), node.b);
    const NodeId* params = ast_.list(node);
    std::vector<std::uint16_t> regs(node.count);
    for (std::uint32_t k = 0; k < node.count; ++k)
        if (!fb_->alloc(regs[k])) return builder_ok();
    fb_->set_params(static_cast<std::uint16_t>(node.count));
    for (std::uint32_t k = 0; k < node.count; ++k) {
        const Node& param = ast_.at(params[k]);
        const std::string_view name = text(param);
        if (plan_.index.count(name)) return fail(param, "param-shadows-fn");
        Binding binding;
        binding.reg = regs[k];
        binding.typed = true;
        std::uint16_t untyped = 0;
        if (!fb_->alloc(binding.type_reg) || !fb_->constant(Value::integer(-1), untyped)) return builder_ok();
        fb_->set_offset(param.begin);
        // Call sites check that the argument's text type agrees with its value (CheckParam).
        fb_->emit(OpCode::BindType, binding.type_reg, binding.reg, untyped);
        if (!scopes_.declare(name, binding)) return fail(param, "param-duplicate");
    }
    // The body shares the parameters' scope: `p := ...` redeclares a parameter.
    if (!statements(ast_.at(node.b))) return false;
    fb_->emit(OpCode::ReturnNull);
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
    fb_->set_offset(node.begin);
    bool ok = false;
    switch (node.kind) {
        case NodeKind::Comment: return fail(node, "shebang-statement");  // only `#!` statements reach the AST
        case NodeKind::Declare: return declare(node) && builder_ok();  // keeps its binding registers
        case NodeKind::ExprStmt: ok = expression_statement(node); break;
        case NodeKind::Assign: ok = assign(node); break;
        case NodeKind::CompoundAssign: ok = compound_assign(node); break;
        case NodeKind::IncDec: ok = inc_dec(node); break;
        case NodeKind::If: ok = if_chain(node); break;
        case NodeKind::While: ok = while_loop(node); break;
        case NodeKind::Return: ok = return_statement(node); break;
        case NodeKind::Break: case NodeKind::Continue: ok = loop_jump(node); break;
        default: return fail(node, std::string("statement:") + node_kind_name(node.kind));
    }
    fb_->release_to(mark);
    return ok && builder_ok();
}

bool UnitCompiler::new_binding(std::string_view name, const Node& at, std::uint16_t& reg, Binding& binding) {
    if (reserved_name(name)) return fail(at, "binding-name-reserved");
    if (plan_.index.count(name)) return fail(at, "binding-shadows-fn");
    // The type register comes first so the value register is the newest one
    // (a call initialiser then returns in place).
    binding.typed = true;  // every binding: assignments check it, bare references propagate it
    if (!fb_->alloc(binding.type_reg) || !fb_->alloc(reg)) return builder_ok();
    binding.reg = reg;
    return true;
}

bool UnitCompiler::declare(const Node& node) {
    if (node.flags & (kFlagConst | kFlagImmut)) return fail(node, "declare-const");
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(node, "declare-pattern");
    const std::string_view name = text(target);
    std::uint16_t reg = 0;
    Binding binding;
    if (!new_binding(name, target, reg, binding)) return false;
    if (!expr_to(node.b, reg)) return false;
    const NodeKind value_kind = ast_.at(node.b).kind;
    fb_->set_offset(node.begin);
    // Declaring from a place makes a legacy location reference to aggregates.
    if (value_kind == NodeKind::Ident || value_kind == NodeKind::Member || value_kind == NodeKind::Index)
        fb_->emit(OpCode::NoAlias, reg);
    std::uint16_t text_type = 0;
    const TextType inferred = infer_text_type(type_text(ast_.at(node.b)));
    if (!text_type_of(inferred, ast_.at(node.b), text_type)) return false;
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::BindType, binding.type_reg, reg, text_type);
    // A declaration referring to another binding inside prepared-reachable
    // code was refused (rt/prepared.h, review round 2 M1/N1); elsewhere legacy
    // types it from that binding's type at run time, which BindType reproduces.
    if (!scopes_.declare(name, binding)) return fail(node, "static-error:redeclared");
    return true;
}

bool UnitCompiler::assign(const Node& node) {
    const Node& target = ast_.at(node.a);
    if (target.kind == NodeKind::Member) return fail(node, "static-error:member-assign");  // plain objects reject it
    if (target.kind == NodeKind::Index) {
        const Node& object = ast_.at(target.a);
        std::uint16_t reg = 0, key = 0, value = 0;
        if (object.kind != NodeKind::Ident || !local_register(object, reg)) return fail(target, "index-assign-target");
        if (has_call(target.b) && has_call(node.b)) return fail(target, "index-assign-order");
        if (!expr_any(target.b, key) || !expr_any(node.b, value)) return false;
        fb_->set_offset(node.begin);
        fb_->emit(OpCode::SetIndex, reg, key, value);
        return true;
    }
    if (target.kind != NodeKind::Ident) return fail(target, "assign-target");
    const Binding* binding = scopes_.find(text(target));
    if (!binding) return fail(target, "assign-free-name");
    const Binding b = *binding;
    std::uint16_t value = 0;
    if (!temp(value) || !expr_to(node.b, value)) return false;
    const NodeKind value_kind = ast_.at(node.b).kind;
    fb_->set_offset(node.begin);
    if (value_kind == NodeKind::Ident || value_kind == NodeKind::Member || value_kind == NodeKind::Index)
        fb_->emit(OpCode::NoAlias, value);
    std::uint16_t text_type = 0;
    if (!text_type_operand(node.b, text_type)) return false;
    fb_->set_offset(node.begin);
    // With a text type of none or int both legacy paths check the value's own type.
    if (fb_->is_untyped_constant(text_type)) fb_->emit(OpCode::CheckType, value, b.type_reg);
    else fb_->emit(OpCode::CheckAssign, value, b.type_reg, text_type);
    fb_->emit(OpCode::Move, b.reg, value);
    return true;
}

bool UnitCompiler::compound_assign(const Node& node) {
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(target, "compound-target");
    const Binding* binding = scopes_.find(text(target));
    if (!binding) return fail(target, "assign-free-name");
    const Binding b = *binding;
    std::uint16_t value = 0;
    const Node& rhs = ast_.at(node.b);
    Value small;
    if (node.op == Op::AddAssign && rhs.kind == NodeKind::Number && decode_number_literal(text(rhs), small) &&
        small.is(Kind::Int) && small.as_int() <= 32767) {
        fb_->emit(OpCode::AddI, b.reg, b.reg, jump_operand(static_cast<int>(small.as_int())));
        fb_->emit(OpCode::CheckType, b.reg, b.type_reg);
        return true;
    }
    if (!expr_any(node.b, value)) return false;
    fb_->set_offset(node.begin);
    switch (node.op) {
        case Op::AddAssign: fb_->emit(OpCode::AddAssign, b.reg, value); break;
        case Op::SubAssign: fb_->emit(OpCode::Sub, b.reg, b.reg, value); break;
        case Op::MulAssign: fb_->emit(OpCode::Mul, b.reg, b.reg, value); break;
        case Op::DivAssign: fb_->emit(OpCode::Div, b.reg, b.reg, value); break;
        case Op::ModAssign: fb_->emit(OpCode::Mod, b.reg, b.reg, value); break;
        default: return fail(node, "compound-operator");
    }
    fb_->emit(OpCode::CheckType, b.reg, b.type_reg);
    return true;
}

bool UnitCompiler::inc_dec(const Node& node) {
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(target, "incdec-target");
    const Binding* binding = scopes_.find(text(target));
    if (!binding) return fail(target, "assign-free-name");
    const bool up = node.op == Op::PreInc || node.op == Op::PostInc;
    fb_->emit(OpCode::AddI, binding->reg, binding->reg, jump_operand(up ? 1 : -1));
    fb_->emit(OpCode::CheckType, binding->reg, binding->type_reg);
    return true;
}

bool UnitCompiler::if_chain(const Node& node) {
    std::vector<std::size_t> to_else;
    if (!false_jump(node.a, to_else)) return false;
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
    Loop loop;
    loop.start = fb_->here();
    loops_.push_back(loop);
    std::vector<std::size_t> exits;
    if (!false_jump(node.a, exits)) return false;
    scopes_.push();
    const std::uint16_t mark = fb_->top();
    const bool ok = statements(ast_.at(node.b));
    fb_->release_to(mark);
    scopes_.pop();
    if (!ok) return false;
    fb_->set_offset(node.begin);
    const std::size_t back = fb_->emit(OpCode::Jmp);
    fb_->patch(back, loops_.back().start);
    for (std::size_t at : exits) fb_->patch(at, fb_->here());
    for (std::size_t at : loops_.back().breaks) fb_->patch(at, fb_->here());
    loops_.pop_back();
    return builder_ok();
}

bool UnitCompiler::return_statement(const Node& node) {
    if (node.flags & kFlagBare || node.a == kNoNode) {
        fb_->emit(in_function_ ? OpCode::ReturnNull : OpCode::Halt);
        return true;
    }
    std::uint16_t value = 0;
    if (!temp(value) || !expr_to(node.a, value)) return false;
    fb_->set_offset(node.begin);
    const NodeKind kind = ast_.at(node.a).kind;
    const bool place = kind == NodeKind::Ident || kind == NodeKind::Member || kind == NodeKind::Index;
    // Returning a place makes a legacy location reference to an aggregate (c = 1: check).
    fb_->emit(in_function_ ? OpCode::Return : OpCode::TopReturn, value, 0, place ? 1 : 0);
    return true;
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
            if (!expr_any(ast_.list(expr)[0], value)) return false;
            fb_->set_offset(expr.begin);
            fb_->emit(OpCode::Push, reg, value);
            return true;
        }
    }
    // Legacy renders other expression statements as template text (an
    // aggregate value is an error there); only calls are compiled.
    if (expr.kind != NodeKind::Call) return fail(expr, "expression-statement");
    std::uint16_t scratch = 0;
    return temp(scratch) && expr_to(node.a, scratch);
}

} // namespace detail

CompileResult compile_unit(const Ast& ast, NodeId root) {
    CompileResult result;
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
    detail::UnitCompiler compiler(ast, plan);
    auto program = std::make_unique<Program>();
    program->protos.resize(plan.functions.size() + 1);
    bool ok = compiler.compile_main(root, program->protos[0]);
    for (std::size_t k = 0; ok && k < plan.functions.size(); ++k)
        ok = compiler.compile_function(plan.functions[k], program->protos[k + 1]);
    if (!ok) {
        result.reason = compiler.reason();
        result.offset = compiler.offset();
        return result;
    }
    result.program = std::move(program);
    return result;
}

} // namespace nift::rt
