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
    binding.decl_node = node_id(at);
    if (captured_binding(node_id(at))) {
        if (!fb_->alloc(binding.cell_reg)) return builder_ok();
        binding.cell = true;
        binding.reg = binding.cell_reg;
    }
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
    // CP12b structs: `p := Name(args)` makes the instance the binding holds.
    const Node& init = ast_.at(node.b);
    const auto made = init.kind == NodeKind::Call && ast_.at(init.a).kind == NodeKind::Ident
                          ? plan_.struct_index.find(text(ast_.at(init.a)))
                          : plan_.struct_index.end();
    if (made != plan_.struct_index.end()) {
        binding.struct_id = static_cast<int>(made->second);
        if (!struct_construct(node, binding.struct_id, reg)) return false;
    } else if (!no_alias(node.b, node) || !expr_to(node.b, reg)) {
        return false;
    }
    std::uint16_t text_type = 0;
    const TextType& inferred = text_type_at(node.b);
    binding.decl_kinds = kinds(node.b);
    binding.decl_text_double = inferred.kind == TextType::Kind::Known && inferred.known == 3;
    binding.decl_may_double = may_double(inferred, scope_index());
    if (!text_type_of(inferred, ast_.at(node.b), text_type)) return false;
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::BindType, binding.type_reg, reg, text_type);
    if (binding.cell) fb_->emit(OpCode::NewCell, binding.cell_reg, reg, binding.type_reg);
    // A declaration in loop code whose binding type legacy's prepared path
    // would derive differently was refused (rt/prepared.h, prepared-decl-type).
    if (!scopes_.declare(name, binding)) return fail(node, "static-error:redeclared");
    note_global_declaration(node, binding);  // CP12b: a global some function reaches
    return true;
}

// `x = e`: the canonical check of the type legacy derives from the text and
// the value (nift_binding_type_from_text). Legacy's prepared path checks the
// value's type only (P4); in loop code where the two could disagree the
// assignment is refused (D-018, context:assignment-type). For a numeric
// value they cannot disagree when the target's binding type is always double
// (both verdicts accept any number) or when the text's type is the target's
// own (`x = x`); a string held by a double-typed binding (`s := "" + 1.5`)
// fails only the prepared check, so other values get no exemption.
bool UnitCompiler::assign_value(const Node& node, const Binding& b) {
    const TextType& type = text_type_at(node.b);
    const std::string_view target = text(ast_.at(node.a));
    const bool numeric = (kinds(node.b) & static_cast<KindSet>(~kKindNumber)) == 0;
    const bool own_type = type.kind == TextType::Kind::Binding && type.binding == target;
    const bool exempt = numeric && (own_type || always_double_[binding_scope(b)].count(target));
    if (loop_reachable() && !exempt && type_mismatch_possible(type, kinds(node.b), scope_index()))
        return fail(node, "context:assignment-type");
    if (loop_reachable() && binding_error_possible(b, kinds(node.b))) return fail(node, "context:assignment-error");
    std::uint16_t value = 0;
    if (!temp(value) || !expr_to(node.b, value)) return false;
    std::uint16_t text_type = 0;
    if (!text_type_of(type, ast_.at(node.b), text_type)) return false;
    if (b.lazy_for) {
        if (b.cell) {
            std::uint16_t untyped = 0;
            if (!fb_->constant(Value::integer(-1), untyped)) return builder_ok();
            fb_->emit(OpCode::CellStore, b.reg, value, untyped);
        } else if (b.reg != value) {
            fb_->emit(OpCode::Move, b.reg, value);
        }
        fb_->emit(OpCode::LoadI, b.lazy_active, 0);
        return builder_ok();
    }
    if (b.cell || b.upvalue) {
        fb_->set_offset(node.begin);
        const std::uint16_t mark = fb_->top();
        if (!store_binding(b, value, text_type)) return false;
        fb_->note(fb_->here() - 1u, std::string(text(ast_.at(node.a))));
        fb_->release_to(mark);
        return builder_ok();
    }
    std::uint16_t binding_type = b.type_reg;
    if (b.global) {
        // D2: every global write is guarded, not only plain reads.
        fb_->set_offset(node.begin);
        if (!global_check_defined(b.reg, target)) return false;
        if (!global_read(b.type_reg, binding_type)) return false;  // CP12b
    }
    fb_->set_offset(node.begin);
    const std::size_t check = fb_->is_untyped_constant(text_type)
                                  ? fb_->emit(OpCode::CheckType, value, binding_type)
                                  : fb_->emit(OpCode::CheckAssign, value, binding_type, text_type);
    fb_->note(check, std::string(text(ast_.at(node.a))));
    if (b.global) global_put(b, value);
    else fb_->emit(OpCode::Move, b.reg, value);
    return true;
}

bool UnitCompiler::assign(const Node& node) {
    const Node& target = ast_.at(node.a);
    if (target.kind == NodeKind::Member) {
        bool is_this = false;  // CP12b: a struct field
        const int s = struct_of_receiver(target.a, is_this);
        const StructFieldInfo* field = nullptr;
        if (s >= 0) return field_access_ok(target, s, text(ast_.at(target.b)), is_this, field) &&
                           field_assign(node, target.a, *field);
        return fail(node, "static-error:member-assign");  // plain objects reject it
    }
    if (target.kind == NodeKind::Index) {
        const Node& object = ast_.at(target.a);
        std::uint16_t reg = 0, key = 0, value = 0;
        const Binding* holder = object.kind == NodeKind::Ident ? find_binding(text(object)) : nullptr;
        const bool global = holder != nullptr && holder->global;
        const bool indirect = holder != nullptr && (holder->cell || holder->upvalue);
        const Binding held = (global || indirect) ? *holder : Binding();
        if (object.kind != NodeKind::Ident || (!global && !local_register(object, reg)))
            return fail(target, "index-assign-target");
        if (!element_target_ok(target)) return false;
        if (!kinds_within(target.a, kKindArray, target, "type:element-assignment") ||
            !kinds_within(target.b, kKindNumber, target, "type:element-assignment") || !no_alias(node.b, node))
            return false;
        if (!expr_any(target.b, key) || !expr_any(node.b, value)) return false;
        if (global) {
            fb_->set_offset(target.begin);
            if (!global_check_defined(held.reg, text(object))) return false;
        }
        if (global && !global_take(held, reg, text(object))) return false;  // CP12b: in place, after the operands
        fb_->set_offset(node.begin);
        const std::size_t at = fb_->emit(OpCode::SetIndex, reg, key, value);
        fb_->note(at, std::string(text(ast_.at(target.b))));
        if (global) global_put(held, reg);
        else if (indirect) {
            std::uint16_t untyped = 0;
            if (!fb_->constant(Value::integer(-1), untyped)) return builder_ok();
            if (!store_binding(held, reg, untyped)) return false;
        }
        return true;
    }
    if (target.kind != NodeKind::Ident) return fail(target, "assign-target");
    const Binding* binding = find_binding(text(target));
    if (!binding)
        if (const StructFieldInfo* field = own_field(text(target)))  // CP12b: an implicit field
            return field_in_loop_code() ? fail(target, "context:struct-field") : field_assign(node, kNoNode, *field);
    if (!binding) return fail(target, "assign-free-name");
    if (binding->struct_id >= 0) return fail(target, "struct-assign");
    const Binding b = *binding;
    if (!no_alias(node.b, node)) return false;
    return assign_value(node, b);
}

bool UnitCompiler::compound_op(const Node& node, const Binding& b) {
    const Node& target = ast_.at(node.a);
    const std::uint16_t mark = fb_->top();
    // CP12b: a global is updated in a register taken from main's slot (after
    // the right-hand side, which may read it) and stored back.
    std::uint16_t reg = b.reg, binding_type = b.type_reg, value = 0;
    const bool indirect = b.cell || b.upvalue;
    const bool lazy = b.lazy_for;
    bool lazy_temp = false;
    Value small;
    const Node* rhs = node.kind == NodeKind::IncDec ? nullptr : &ast_.at(node.b);
    const bool immediate = rhs != nullptr && node.op == Op::AddAssign && rhs->kind == NodeKind::Number &&
                           decode_number_literal(text(*rhs), small) && small.is(Kind::Int) && small.as_int() <= 32767;
    if (rhs != nullptr && !immediate && !expr_any(node.b, value)) return false;
    if (lazy) {
        if (!for_read_source(b, reg, lazy_temp)) return false;
    } else if (indirect) {
        if (!read_binding(b, reg)) return false;
    } else if (b.global && ((loop_reachable() &&
                             (fb_->set_offset(node.begin), !global_check_defined(b.reg, text(target)))) ||
                            !global_take(b, reg, text(target)) || !global_read(b.type_reg, binding_type))) {
        return false;
    }
    fb_->set_offset(node.begin);
    if (node.kind == NodeKind::IncDec) {
        const bool up = node.op == Op::PreInc || node.op == Op::PostInc;
        fb_->emit(OpCode::AddI, reg, reg, jump_operand(up ? 1 : -1));
    } else if (immediate) {
        fb_->emit(OpCode::AddI, reg, reg, jump_operand(static_cast<int>(small.as_int())));
    } else {
        switch (node.op) {
            case Op::AddAssign: fb_->emit(OpCode::AddAssign, reg, value); break;
            case Op::SubAssign: fb_->emit(OpCode::Sub, reg, reg, value); break;
            case Op::MulAssign: fb_->emit(OpCode::Mul, reg, reg, value); break;
            case Op::DivAssign: fb_->emit(OpCode::Div, reg, reg, value); break;
            case Op::ModAssign: fb_->emit(OpCode::Mod, reg, reg, value); break;
            default: return fail(node, "compound-operator");
        }
    }
    if (loop_reachable() && binding_error_possible(b, kinds(node.a))) return fail(node, "context:assignment-error");
    if (lazy) {
        if (b.cell) {
            std::uint16_t untyped = 0;
            if (!fb_->constant(Value::integer(-1), untyped)) return builder_ok();
            fb_->emit(OpCode::CellStore, b.reg, reg, untyped);
        } else if (b.reg != reg) {
            fb_->emit(OpCode::Move, b.reg, reg);
        }
        fb_->emit(OpCode::LoadI, b.lazy_active, 0);
        if (lazy_temp) fb_->release_to(mark);
        else fb_->release_to(mark);
        return builder_ok();
    }
    if (indirect) {
        std::uint16_t untyped = 0;
        if (!fb_->constant(Value::integer(-1), untyped)) return builder_ok();
        if (!store_binding(b, reg, untyped)) return false;
        fb_->note(fb_->here() - 1u, std::string(text(target)));
    } else {
        const std::size_t check = fb_->emit(OpCode::CheckType, reg, binding_type);
        fb_->note(check, std::string(text(target)));
        if (b.global) global_put(b, reg);
    }
    fb_->release_to(mark);
    return builder_ok();
}

bool UnitCompiler::compound_assign(const Node& node) {
    const Node& target = ast_.at(node.a);
    if (const StructFieldInfo* field = struct_target(target))
        return field_update(node, target_receiver(target), *field);
    if (!reason_.empty()) return false;
    if (target.kind != NodeKind::Ident) return fail(target, "compound-target");
    const Binding* binding = find_binding(text(target));
    if (!binding) return fail(target, "assign-free-name");
    if (binding->struct_id >= 0) return fail(target, "struct-assign");
    const Binding b = *binding;
    // `+=` on two numbers or two strings, the others on numbers (legacy
    // errors and prepared array concatenation are refused).
    const KindSet l = kinds(node.a), r = kinds(node.b);
    const bool numbers = (l & ~kKindNumber) == 0 && (r & ~kKindNumber) == 0;
    const bool strings = node.op == Op::AddAssign && l == kKindString && r == kKindString;
    if (!numbers && !strings && !unknown_defers(l | r))  // Unknown is neither (review D1)
        return fail(node, "type:compound-assignment");
    if (loop_reachable()) {
        // Legacy's prepared `%=` computes fmod on any numbers (the canonical
        // rule requires integer-valued operands).
        if (node.op == Op::ModAssign && ((l | r) & (kKindFrac | kKindUnknown)))
            return fail(node, "context:modulo-assignment");
        // Legacy's prepared `s += t` on strings skips the binding type check
        // (execute_prepared appends in place), so a binding typed double that
        // holds a string keeps it there and fails canonically.
        if (node.op == Op::AddAssign && ((l | r) & kKindString) && may_double_[binding_scope(b)].count(text(target)))
            return fail(node, "context:string-append-type");
    }
    return compound_op(node, b);
}

bool UnitCompiler::inc_dec(const Node& node) {
    const Node& target = ast_.at(node.a);
    if (const StructFieldInfo* field = struct_target(target))
        return field_update(node, target_receiver(target), *field);
    if (!reason_.empty()) return false;
    if (target.kind != NodeKind::Ident) return fail(target, "incdec-target");
    const Binding* binding = find_binding(text(target));
    if (!binding) return fail(target, "assign-free-name");
    if (binding->struct_id >= 0) return fail(target, "struct-assign");
    const Binding b = *binding;
    if (!kinds_within(node.a, kKindNumber, node, "type:increment")) return false;
    return compound_op(node, b);
}

bool UnitCompiler::inc_dec_expr(const Node& node, std::uint16_t dst) {
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(target, "incdec-target");
    const Binding* found = find_binding(text(target));
    if (!found) return fail(target, "assign-free-name");
    const Binding b = *found;
    if (!kinds_within(node.a, kKindNumber, node, "type:increment")) return false;
    const std::uint16_t mark = fb_->top();
    std::uint16_t work = 0;
    if (!temp(work)) return false;
    bool lazy_temp = false;
    if (b.lazy_for) {
        if (!for_read_source(b, work, lazy_temp)) return false;
    } else if (b.global) {
        if (loop_reachable() && (fb_->set_offset(node.begin), !global_check_defined(b.reg, text(target)))) return false;
        if (!global_read_value(b.reg, work, text(target))) return false;
    } else if (!read_binding(b, work)) {
        return false;
    }
    if (work == b.reg) {
        std::uint16_t copy = 0;
        if (!temp(copy)) return false;
        fb_->emit(OpCode::Move, copy, work);
        work = copy;
    }
    const bool post = node.op == Op::PostInc || node.op == Op::PostDec;
    if (b.loop_binding && !post) {
        // Legacy's expression parser treats `++x`/`--x` on a loop location as
        // an unapplied prefix read, not a conventional increment/decrement.
        if (work != dst) fb_->emit(OpCode::Move, dst, work);
        fb_->release_to(mark);
        return builder_ok();
    }
    if (post) fb_->emit(OpCode::Move, dst, work);
    const bool up = node.op == Op::PreInc || node.op == Op::PostInc;
    fb_->emit(OpCode::AddI, work, work, jump_operand(up ? 1 : -1));
    std::uint16_t untyped = 0;
    if (!fb_->constant(Value::integer(-1), untyped)) return false;
    if (b.lazy_for) {
        if (b.cell) {
            fb_->emit(OpCode::CellStore, b.reg, work, untyped);
        } else if (b.reg != work) {
            fb_->emit(OpCode::Move, b.reg, work);
        }
        fb_->emit(OpCode::LoadI, b.lazy_active, 0);
    } else if (b.cell || b.upvalue) {
        if (!store_binding(b, work, untyped)) return false;
        fb_->note(fb_->here() - 1u, std::string(text(target)));
    } else {
        std::uint16_t binding_type = b.type_reg;
        if (b.global && !global_read(b.type_reg, binding_type)) return false;
        const std::size_t check = fb_->emit(OpCode::CheckType, work, binding_type);
        fb_->note(check, std::string(text(target)));
        if (b.global) global_put(b, work);
        else if (work != b.reg) fb_->emit(OpCode::Move, b.reg, work);
    }
    if (!post) fb_->emit(OpCode::Move, dst, work);
    fb_->release_to(mark);
    return builder_ok();
}

} // namespace nift::rt::detail
