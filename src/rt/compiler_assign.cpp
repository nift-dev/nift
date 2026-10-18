#include "rt/compiler_assign.h"

#include "rt/literal.h"
#include "rt/native.h"

// Declarations, assignments, compound assignments and increments (CP11, CP12),
// including the textual re-evaluation code legacy runs for a prepared
// statement whose value is a large number.

namespace nift::rt::detail {

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

// A prepared declaration, assignment, compound assignment or increment whose
// value (or operand) legacy calls large is re-evaluated by legacy on its
// textual path (ParserTemplate.cpp, large_num): such statements are compiled
// twice, the textual code next to the prepared code (CP12; P6).
bool UnitCompiler::reevaluated(const Node& node, NodeId value) const {
    if (!prepared()) return false;
    switch (node.kind) {
        case NodeKind::Declare: case NodeKind::Assign: return (kinds(value) & kKindNumber) != 0;
        case NodeKind::CompoundAssign: case NodeKind::IncDec: return (kinds(node.a) & kKindNumber) != 0;
        default: return false;
    }
}

bool UnitCompiler::declare_value(const Node& node, std::uint16_t reg) {
    if (!no_alias(node.b, node)) return false;
    if (!reevaluated(node, node.b)) return expr_to(node.b, reg);
    if (!expr_to(node.b, reg)) return false;
    fb_->set_offset(node.begin);
    const std::size_t large = fb_->emit(OpCode::JmpLarge, reg);
    const std::size_t join = fb_->emit(OpCode::Jmp);
    fb_->patch(large, fb_->here());
    textual_statement_ = true;
    update_flags();
    const bool ok = expr_to(node.b, reg);
    textual_statement_ = false;
    update_flags();
    fb_->patch(join, fb_->here());
    return ok && builder_ok();
}

bool UnitCompiler::declare(const Node& node) {
    if (node.flags & (kFlagConst | kFlagImmut)) return fail(node, "declare-const");
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(node, "declare-pattern");
    const std::string_view name = text(target);
    std::uint16_t reg = 0;
    Binding binding;
    if (!new_binding(name, target, reg, binding)) return false;
    if (!declare_value(node, reg)) return false;
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

// The value check and move of `x = e`, for the current evaluator: the textual
// path checks the type legacy derives from the text and the value
// (nift_binding_type_from_text), the prepared path the value's type (P4).
bool UnitCompiler::assign_value(const Node& node, const Binding& b) {
    std::uint16_t value = 0;
    if (!temp(value) || !expr_to(node.b, value)) return false;
    fb_->set_offset(node.begin);
    std::size_t check = 0;
    if (prepared()) {
        check = fb_->emit(OpCode::CheckType, value, b.type_reg);
    } else {
        std::uint16_t text_type = 0;
        if (!text_type_operand(node.b, text_type)) return false;
        fb_->set_offset(node.begin);
        check = fb_->is_untyped_constant(text_type) ? fb_->emit(OpCode::CheckType, value, b.type_reg)
                                                    : fb_->emit(OpCode::CheckAssign, value, b.type_reg, text_type);
    }
    fb_->note(check, std::string(text(ast_.at(node.a))));
    caller_site(check);
    fb_->emit(OpCode::Move, b.reg, value);
    return true;
}

bool UnitCompiler::assign(const Node& node) {
    const Node& target = ast_.at(node.a);
    if (target.kind == NodeKind::Member) return fail(node, "static-error:member-assign");  // plain objects reject it
    if (target.kind == NodeKind::Index) {
        const Node& object = ast_.at(target.a);
        std::uint16_t reg = 0, key = 0, value = 0;
        if (object.kind != NodeKind::Ident || !local_register(object, reg)) return fail(target, "index-assign-target");
        if (!element_target_ok(target)) return false;
        if (!kinds_within(target.a, kKindArray, target, "type:element-assignment") ||
            !kinds_within(target.b, kKindNumber, target, "type:element-assignment") || !no_alias(node.b, node))
            return false;
        if (!expr_any(target.b, key) || !expr_any(node.b, value)) return false;
        fb_->set_offset(node.begin);
        const std::size_t at = fb_->emit(OpCode::SetIndex, reg, key, value);
        fb_->note(at, std::string(text(ast_.at(target.b))));
        caller_site(at);
        return true;
    }
    if (target.kind != NodeKind::Ident) return fail(target, "assign-target");
    const Binding* binding = scopes_.find(text(target));
    if (!binding) return fail(target, "assign-free-name");
    const Binding b = *binding;
    if (!no_alias(node.b, node)) return false;
    if (!reevaluated(node, node.b)) return assign_value(node, b);
    // Prepared code, then (for a large value) the statement's textual code.
    const std::uint16_t mark = fb_->top();
    std::uint16_t value = 0;
    if (!temp(value) || !expr_to(node.b, value)) return false;
    fb_->set_offset(node.begin);
    const std::size_t large = fb_->emit(OpCode::JmpLarge, value);
    const std::size_t check = fb_->emit(OpCode::CheckType, value, b.type_reg);
    fb_->note(check, std::string(text(target)));
    caller_site(check);
    fb_->emit(OpCode::Move, b.reg, value);
    const std::size_t done = fb_->emit(OpCode::Jmp);
    fb_->release_to(mark);
    fb_->patch(large, fb_->here());
    textual_statement_ = true;
    update_flags();
    const bool ok = assign_value(node, b);
    textual_statement_ = false;
    update_flags();
    fb_->patch(done, fb_->here());
    return ok && builder_ok();
}

bool UnitCompiler::compound_op(const Node& node, const Binding& b, bool prepared_code) {
    const Node& target = ast_.at(node.a);
    const std::uint16_t mark = fb_->top();
    std::size_t op = 0;
    if (node.kind == NodeKind::IncDec) {
        const bool up = node.op == Op::PreInc || node.op == Op::PostInc;
        fb_->set_offset(node.begin);
        op = fb_->emit(OpCode::AddI, b.reg, b.reg, jump_operand(up ? 1 : -1));
    } else {
        const Node& rhs = ast_.at(node.b);
        Value small;
        std::uint16_t value = 0;
        if (node.op == Op::AddAssign && rhs.kind == NodeKind::Number && decode_number_literal(text(rhs), small) &&
            small.is(Kind::Int) && small.as_int() <= 32767) {
            fb_->set_offset(node.begin);
            op = fb_->emit(OpCode::AddI, b.reg, b.reg, jump_operand(static_cast<int>(small.as_int())));
        } else {
            if (!expr_any(node.b, value)) return false;
            fb_->set_offset(node.begin);
            switch (node.op) {
                case Op::AddAssign: op = fb_->emit(OpCode::AddAssign, b.reg, value); break;
                case Op::SubAssign: op = fb_->emit(OpCode::Sub, b.reg, b.reg, value); break;
                case Op::MulAssign: op = fb_->emit(OpCode::Mul, b.reg, b.reg, value); break;
                case Op::DivAssign: op = fb_->emit(OpCode::Div, b.reg, b.reg, value); break;
                case Op::ModAssign: op = fb_->emit(OpCode::Mod, b.reg, b.reg, value); break;
                default: return fail(node, "compound-operator");
            }
        }
    }
    if (prepared_code) fb_->at(op).flags |= kInstrReeval;
    caller_site(op);
    const std::size_t check = fb_->emit(OpCode::CheckType, b.reg, b.type_reg);
    fb_->note(check, std::string(text(target)));
    caller_site(check);
    fb_->release_to(mark);
    return builder_ok();
}

bool UnitCompiler::compound_assign(const Node& node) {
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(target, "compound-target");
    const Binding* binding = scopes_.find(text(target));
    if (!binding) return fail(target, "assign-free-name");
    const Binding b = *binding;
    // `+=` on two numbers or two strings, the others on numbers (legacy
    // errors and prepared array concatenation are refused).
    const std::uint8_t l = kinds(node.a), r = kinds(node.b);
    const bool numbers = (l & ~kKindNumber) == 0 && (r & ~kKindNumber) == 0;
    const bool strings = node.op == Op::AddAssign && l == kKindString && r == kKindString;
    if (!numbers && !strings && !((l | r) & kKindUnknown)) return fail(node, "type:compound-assignment");
    if (!reevaluated(node, node.b)) return compound_op(node, b, false);
    std::size_t note = 0;
    {
        const std::size_t first = fb_->here();
        if (!compound_op(node, b, true)) return false;
        // The flagged operation is the first instruction with kInstrReeval.
        for (std::size_t i = first; i < fb_->here(); ++i)
            if (fb_->at(i).flags & kInstrReeval) note = fb_->note(i, std::string());
    }
    const std::size_t done = fb_->emit(OpCode::Jmp);
    fb_->set_note_alt(note, fb_->here());
    textual_statement_ = true;
    update_flags();
    const bool ok = compound_op(node, b, false);
    textual_statement_ = false;
    update_flags();
    fb_->patch(done, fb_->here());
    return ok && builder_ok();
}

bool UnitCompiler::inc_dec(const Node& node) {
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(target, "incdec-target");
    const Binding* binding = scopes_.find(text(target));
    if (!binding) return fail(target, "assign-free-name");
    const Binding b = *binding;
    if (!kinds_within(node.a, kKindNumber, node, "type:increment")) return false;
    if (!reevaluated(node, node.a)) return compound_op(node, b, false);
    const std::size_t first = fb_->here();
    if (!compound_op(node, b, true)) return false;
    std::size_t note = 0;
    for (std::size_t i = first; i < fb_->here(); ++i)
        if (fb_->at(i).flags & kInstrReeval) note = fb_->note(i, std::string());
    const std::size_t done = fb_->emit(OpCode::Jmp);
    fb_->set_note_alt(note, fb_->here());
    textual_statement_ = true;
    update_flags();
    const bool ok = compound_op(node, b, false);
    textual_statement_ = false;
    update_flags();
    fb_->patch(done, fb_->here());
    return ok && builder_ok();
}

} // namespace nift::rt::detail
