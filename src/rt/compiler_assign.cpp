#include "rt/compiler_assign.h"

#include "rt/literal.h"
#include "rt/native.h"

// Declarations, assignments, compound assignments and increments (CP11,
// CP12). One meaning everywhere (D-018): legacy's canonical (textual)
// binding-type rules and CP09 arithmetic; constructs whose legacy behaviour
// in loop code (legacy's prepared evaluator) could differ from that are
// refused before execution ("context:*").

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

bool UnitCompiler::declare(const Node& node) {
    if (node.flags & (kFlagConst | kFlagImmut)) return fail(node, "declare-const");
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(node, "declare-pattern");
    const std::string_view name = text(target);
    std::uint16_t reg = 0;
    Binding binding;
    if (!new_binding(name, target, reg, binding)) return false;
    if (!no_alias(node.b, node) || !expr_to(node.b, reg)) return false;
    std::uint16_t text_type = 0;
    const TextType inferred = infer_text_type(type_text(ast_.at(node.b)));
    if (!text_type_of(inferred, ast_.at(node.b), text_type)) return false;
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::BindType, binding.type_reg, reg, text_type);
    // A declaration in loop code whose binding type legacy's prepared path
    // would derive differently was refused (rt/prepared.h, prepared-decl-type).
    if (!scopes_.declare(name, binding)) return fail(node, "static-error:redeclared");
    return true;
}

// `x = e`: the canonical check of the type legacy derives from the text and
// the value (nift_binding_type_from_text). Legacy's prepared path checks the
// value's type only (P4); in loop code where the two could disagree the
// assignment is refused (D-018, context:assignment-type). They can only
// disagree for a target whose binding type may be int (a double binding
// accepts both), and not when the text's type is the target's own (`x = x`:
// the textual type is the binding's, or the value's when that is int).
bool UnitCompiler::assign_value(const Node& node, const Binding& b) {
    const TextType type = infer_text_type(type_text(ast_.at(node.b)));
    const std::string_view target = text(ast_.at(node.a));
    const bool own_type = type.kind == TextType::Kind::Binding && type.binding == target;
    if (loop_reachable() && !own_type && type_mismatch_possible(type, kinds(node.b), scope_index()) &&
        !always_double_[scope_index()].count(target))
        return fail(node, "context:assignment-type");
    std::uint16_t value = 0;
    if (!temp(value) || !expr_to(node.b, value)) return false;
    std::uint16_t text_type = 0;
    if (!text_type_of(type, ast_.at(node.b), text_type)) return false;
    fb_->set_offset(node.begin);
    const std::size_t check = fb_->is_untyped_constant(text_type)
                                  ? fb_->emit(OpCode::CheckType, value, b.type_reg)
                                  : fb_->emit(OpCode::CheckAssign, value, b.type_reg, text_type);
    fb_->note(check, std::string(text(ast_.at(node.a))));
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
        return true;
    }
    if (target.kind != NodeKind::Ident) return fail(target, "assign-target");
    const Binding* binding = scopes_.find(text(target));
    if (!binding) return fail(target, "assign-free-name");
    const Binding b = *binding;
    if (!no_alias(node.b, node)) return false;
    return assign_value(node, b);
}

bool UnitCompiler::compound_op(const Node& node, const Binding& b) {
    const Node& target = ast_.at(node.a);
    const std::uint16_t mark = fb_->top();
    if (node.kind == NodeKind::IncDec) {
        const bool up = node.op == Op::PreInc || node.op == Op::PostInc;
        fb_->set_offset(node.begin);
        fb_->emit(OpCode::AddI, b.reg, b.reg, jump_operand(up ? 1 : -1));
    } else {
        const Node& rhs = ast_.at(node.b);
        Value small;
        std::uint16_t value = 0;
        if (node.op == Op::AddAssign && rhs.kind == NodeKind::Number && decode_number_literal(text(rhs), small) &&
            small.is(Kind::Int) && small.as_int() <= 32767) {
            fb_->set_offset(node.begin);
            fb_->emit(OpCode::AddI, b.reg, b.reg, jump_operand(static_cast<int>(small.as_int())));
        } else {
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
        }
    }
    const std::size_t check = fb_->emit(OpCode::CheckType, b.reg, b.type_reg);
    fb_->note(check, std::string(text(target)));
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
    if (loop_reachable()) {
        // Legacy's prepared `%=` computes fmod on any numbers (the canonical
        // rule requires integer-valued operands).
        if (node.op == Op::ModAssign && ((l | r) & (kKindFrac | kKindUnknown)))
            return fail(node, "context:modulo-assignment");
        // Legacy's prepared `s += t` on strings skips the binding type check
        // (execute_prepared appends in place), so a binding typed double that
        // holds a string keeps it there and fails canonically.
        if (node.op == Op::AddAssign && ((l | r) & kKindString) && may_double_[scope_index()].count(text(target)))
            return fail(node, "context:string-append-type");
    }
    return compound_op(node, b);
}

bool UnitCompiler::inc_dec(const Node& node) {
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(target, "incdec-target");
    const Binding* binding = scopes_.find(text(target));
    if (!binding) return fail(target, "assign-free-name");
    const Binding b = *binding;
    if (!kinds_within(node.a, kKindNumber, node, "type:increment")) return false;
    return compound_op(node, b);
}

} // namespace nift::rt::detail
