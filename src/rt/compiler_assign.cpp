#include "rt/compiler_assign.h"
#include "rt/compiler_element_path.h"
#include "rt/binding_rules.h"
#include "rt/literal.h"
#include "rt/module_data.h"
#include "rt/module_link.h"
#include "rt/native.h"
namespace nift::rt::detail {
bool UnitCompiler::new_binding(std::string_view name, const Node& at, std::uint16_t& reg, Binding& binding) {
    if (session_ && plan_.index.count(name)) return fail(at, "binding-shadows-fn");
    // The type register comes first so the value register is the newest one
    // (a call initialiser then returns in place).
    binding.typed = true;  // every binding: assignments check it, bare references propagate it
    if (!fb_->alloc(binding.type_reg) || !fb_->alloc(reg)) return builder_ok();
    binding.reg = reg;
    binding.decl_node = node_id(at);
    if ((session_ && !in_function_) || captured_binding(node_id(at)) || selector_roots_.count(node_id(at))) {
        binding.selector_root_only = !captured_binding(node_id(at));
        binding.location_source = selector_roots_.count(binding.decl_node);
        if (!fb_->alloc(binding.cell_reg)) return builder_ok();
        binding.cell = true;
        binding.reg = binding.cell_reg;
    }
    return true;
}
bool UnitCompiler::declare(const Node& node) {
    const Node& target = ast_.at(node.a);
    // CP25: a `const`/`immut` binding of a proved scalar is an ordinary binding
    // whose every write raises legacy's fatal "cannot assign to const binding"
    // Error. Captured or session constants keep the refusal (the capture and session paths carry no const flag).
    const KindSet init_kinds = kinds(node.b);
    const bool const_decl = (node.flags & (kFlagImmut | kFlagConst)) != 0;
    // In loop-evaluator code legacy's prepared path does not enforce a const
    // declared there; D-009 #2 (const is immutable everywhere) applies.
    const bool const_scalar = const_decl && init_kinds != 0 && (init_kinds & ~kKindScalar) == 0 &&
                              !session_ && target.kind == NodeKind::Ident &&
                              !captured_binding(node_id(target));
    const bool const_object = const_decl && init_kinds == kKindObject &&
        !in_function_ && !in_lambda_ && !loop_reachable() && !session_ && !plan_.coroutines &&
        (!module_names_ || module_names_->empty()) && !captured_binding(node_id(target));
    if (const_decl && !const_scalar && ((node.flags & kFlagImmut) || (init_kinds != kKindArray && !const_object)))
        return fail(node, "declare-const");
    if (target.kind != NodeKind::Ident) return fail(node, "declare-pattern");
    if (!const_decl) {
        const int indexed = declare_indexed_alias(node);
        if (indexed != 0) return indexed > 0;
    }
    const std::string_view name = text(target);
    const Node& init = ast_.at(node.b);
    const bool same_value = name == "same" && init_kinds == kKindBool &&  // rt-lint: allow-string-compare compile time
        init.kind == NodeKind::Binary && (init.op == Op::Eq || init.op == Op::Ne) &&
        !session_ && !in_function_ && !in_lambda_ && !plan_.coroutines &&
        (!module_names_ || module_names_->empty());
    const bool size_value = name == "size" && init.kind == NodeKind::String &&  // rt-lint: allow-string-compare
        init_kinds == kKindString;
    const bool length_value = name == "len" &&  // rt-lint: allow-string-compare
        init.kind == NodeKind::Call && init.count == 0 && ast_.at(init.a).kind == NodeKind::Member &&
        text(ast_.at(ast_.at(init.a).b)) == "length" &&  // rt-lint: allow-string-compare
        !assigned_.count(name);
    const bool method_value = (size_value || length_value) && !session_ && in_function_ && !in_lambda_ &&
        plan_.is_method(function_) && imported_module_data(plan_, node) && !captured_binding(node_id(target)) &&
        !reserved_name_called_in(ast_, name, ast_.at(ast_.at(plan_.functions[function_]).b));
    // A local value binding named `args` in an imported struct method is an
    // ordinary binding in legacy (a parameter-level exception already covers
    // methods, a top-level one covers the entry script). It shadows the
    // script invocation binding, and the module has no generated private
    // args/cmd name that the linker would rename through it.
    const bool method_args_binding = name == "args" && !session_ && in_function_ && !in_lambda_ &&  // rt-lint: allow-string-compare
        plan_.is_method(function_) && imported_module_data(plan_, node) && !captured_binding(node_id(target)) &&
        (!plan_.linked || plan_.linked->graph.modules[plan_.linked->locate(node.begin).module]->generated_names.empty()) &&
        !reserved_name_called_in(ast_, name, ast_.at(ast_.at(plan_.functions[function_]).b));
    // A local value binding in a function may take a built-in's name when it is
    // never called there (legacy resolves the binding; the call split is the
    // same rule as for-loop/catch bindings). `this`/`self` stay receiver names.
    const bool local_reserved_value = reserved_name(name) && in_function_ && !in_lambda_ &&
        name != "this" && name != "self" &&  // rt-lint: allow-string-compare compile time
        !reserved_name_called_in(ast_, name, ast_.at(ast_.at(plan_.functions[function_]).b));
    if (reserved_name(name) && !same_value && !method_value && !method_args_binding && !local_reserved_value &&
        !invocation_shadows_.count(node_id(target)))
        return fail(target, "binding-name-reserved");
    const Binding* parameter = in_lambda_ ? scopes_.find(name) : nullptr;
    if (parameter && parameter->decl_node != kNoNode &&
        ast_.at(parameter->decl_node).kind == NodeKind::Param &&
        !session_ && !loop_reachable() && scopes_.declared_here(name)) {
        // Textual lambda declarations check for a duplicate before evaluating
        // their initializer. The diagnostic is reached only when called.
        const auto mark = fb_->top();
        std::uint16_t scratch = 0;
        if (!temp(scratch) ||
            !legacy_fatal(target, "binding already declared in this scope: " + std::string(name), scratch))
            return false;
        fb_->release_to(mark);
        return builder_ok();
    }
    std::uint16_t reg = 0;
    Binding binding;
    if (!new_binding(name, target, reg, binding)) return false;
    binding.const_array = (node.flags & kFlagConst) != 0 && !const_scalar;
    binding.const_scalar = const_scalar;
    // CP12b structs: `p := Name(args)` makes the instance the binding holds.
    const auto made = init.kind == NodeKind::Call && ast_.at(init.a).kind == NodeKind::Ident
                          ? plan_.struct_index.find(text(ast_.at(init.a)))
                          : plan_.struct_index.end();
    if (made != plan_.struct_index.end()) {
        binding.struct_id = static_cast<int>(made->second);
        if (!struct_construct(ast_.at(node.b), binding.struct_id, reg)) return false;
    } else if (!expr_to(node.b, reg)) {
        return false;
    }
    std::uint16_t text_type = 0;
    const TextType& inferred = text_type_at(node.b);
    binding.decl_kinds = kinds(node.b);
    binding.string_literal_initializer = init.kind == NodeKind::String;
    binding.decl_text_double = inferred.kind == TextType::Kind::Known && inferred.known == 3;
    binding.decl_may_double = may_double(inferred, scope_index());
    // The legacy type probe can see lambda-body operands. An unbound formal
    // has no type at declaration time.
    const bool lambda_initializer = !session_ && (!module_names_ || module_names_->empty()) &&
        ((init.kind == NodeKind::Lambda && !in_function_ && !in_lambda_ && !loop_reachable()) ||
         prepared_lambdas_.count(node.b));
    if (!text_type_of(inferred, init, text_type, lambda_initializer)) return false;
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::BindType, binding.type_reg, reg, text_type);
    if (binding.cell) {
        const auto op = binding.location_source ? OpCode::NewSourceSlot : OpCode::NewCell;
        fb_->emit(op, binding.cell_reg, reg, binding.type_reg);
        if (binding.location_source) fb_->emit(OpCode::LoadNull, reg);
    }
    // A declaration in loop code whose binding type legacy's prepared path
    // would derive differently was refused (rt/prepared.h, prepared-decl-type).
    if (!scopes_.declare(name, binding)) return fail(node, "static-error:redeclared");
    note_global_declaration(node, binding);  // CP12b: a global some function reaches
    return reason_.empty();
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
    if (loop_type_error_ambiguous() && binding_error_possible(b, kinds(node.b))) return fail(node, "context:assignment-error");
    std::uint16_t value = 0;
    if (!temp(value) || !expr_to(node.b, value)) return false;
    std::uint16_t text_type = 0;
    if (!text_type_of(type, ast_.at(node.b), text_type)) return false;
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

bool UnitCompiler::assign(const Node& node, const std::uint16_t* result) {
    const Node& target = ast_.at(node.a);
    if (target.kind == NodeKind::Member) {
        bool is_this = false;  // CP12b: a struct field
        const int s = struct_of_receiver(target.a, is_this);
        const StructFieldInfo* field = nullptr;
        if (s >= 0) return field_access_ok(target, s, text(ast_.at(target.b)), is_this, field) &&
                           field_assign(node, target.a, *field);
        if (kinds(target.a) & kKindStruct) return fail(target, "struct-value");  // D-NEXT-refsem: unproved struct
        return fail(node, "static-error:member-assign");  // plain objects reject it
    }
    if (target.kind == NodeKind::Index) {
        if (ElementPath::form(*this, target)) return ElementPath::store(*this, node, result);  // checkpoint 1
        const Node& object = ast_.at(target.a);
        std::uint16_t reg = 0, key = 0, value = 0;
        const Binding* holder = object.kind == NodeKind::Ident ? find_binding(text(object)) : nullptr;
        // B5 (owner Q2: `const` is binding-only): an element store into a const array is an
        // ordinary store (an immutable object, `args`, fails in the protocol).
        if(holder&&holder->const_scalar)
            return fail(target,write_refusal(holder,text(object),"declare-const"));
        const bool global = holder != nullptr && holder->global;
        const bool indirect = holder != nullptr && (holder->cell || holder->upvalue);
        const Binding held = (global || indirect) ? *holder : Binding();
        if (object.kind != NodeKind::Ident ||
                (!global && !(holder && holder->cell ? indexed_binding(*holder, reg) :
                              local_register(object, reg))))
            return fail(target, "index-assign-target");
        if (!element_target_ok(target)) return false;
        const KindSet receiver_kinds = kinds(target.a), key_kinds = kinds(target.b);
        const bool dynamic_element = plan_.linked && plan_.linked->locate(node.begin).module > 0 &&
            (receiver_kinds == 0 || (receiver_kinds & (kKindUnknown | kKindArray | kKindObject)) != 0) &&
            (key_kinds == 0 || (key_kinds & (kKindUnknown | kKindNull | kKindString | kKindNumber)) != 0);
        if (!dynamic_element &&
            (!kinds_within(target.a, kKindArray, target, "type:element-assignment") ||
             !kinds_within(target.b, kKindNumber, target, "type:element-assignment")))
            return false;
#if !(defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 13)
        // H-A (combined-b3-b5.md): the receiver first, its value retained in a register (a
        // binding's own register is copied when the value can rebind it); then the value,
        // then the index (legacy's order for every context: `b[i] = g()` where g changes i
        // stores at the new i). B2: never taken out of its holder.
        if (global) {
            fb_->set_offset(target.begin);
            if (!global_check_defined(held.reg, text(object))) return false;
            if (!(held.cell ? read_binding(held, reg) : global_read_value(held.reg, reg, text(object)))) return false;
        }
        if (!retain_receiver(reg, !global && !indirect, node.b) || !expr_any(node.b, value) ||
            !expr_any(target.b, key))
            return false;
#else
        if (!expr_any(target.b, key) || !expr_any(node.b, value)) return false;  // M13 (H-A): the old order
        if (global) {
            fb_->set_offset(target.begin);
            if (!global_check_defined(held.reg, text(object))) return false;
        }
        if (indirect && !global && held.selector_root_only && !held.loop_binding)  // B2: re-read, never taken
            fb_->emit(OpCode::CellLoad, reg, held.reg);
        if (global && !(held.cell ? read_binding(held, reg) : global_read_value(held.reg, reg, text(object))))
            return false;
#endif
        fb_->set_offset(node.begin);
        const std::size_t at = fb_->emit(OpCode::SetIndex, reg, key, value);
        fb_->note(at, std::string(text(ast_.at(target.b))));
        // H-A: no write-back when the value can rebind the binding (it would undo the rebinding).
        if (indirect && !global && (held.loop_binding || held.location_source) && !may_rebind(node.b)) {
            std::uint16_t untyped = 0;
            if (!fb_->constant(Value::integer(-1), untyped)) return builder_ok();
            if (!store_indexed_binding(held, reg)) return false;
        }
        if (result) assignment_value(*result, value);  // P1: the value may be a constant operand
        return true;
    }
    if (target.kind != NodeKind::Ident) return fail(target, "assign-target");
    const Binding* binding = find_binding(text(target));
    if (!binding)
        if (const StructFieldInfo* field = own_field(text(target)))  // CP12b: an implicit field
            return field_in_loop_code() ? fail(target, "context:struct-field") : field_assign(node, kNoNode, *field);
    if (!binding || binding->invocation) return fail(target, write_refusal(binding, text(target), "assign-free-name"));
    const bool script_const_object = binding->const_array && binding->decl_kinds == kKindObject &&
        !binding->cell && !binding->global && !in_function_ && !loop_reachable() && !session_;
    if (binding->const_array && !script_const_object) return fail(target, "declare-const");
    if (binding->const_scalar || script_const_object)
        return const_write(node, nullptr, result, *binding, false);  // CP25: before the value
    if (binding->readonly) return fail(target,
        session_ && binding->decl_kinds == kKindFunction ? "session:function-assignment" : "assign-catch-binding");
    if (binding->struct_id >= 0) return fail(target, "struct-assign");
    const Binding b = *binding;
    if (kinds(node.a) & kKindAtomic) return atomic_store(node, b);
    return assign_value(node, b);
}

bool UnitCompiler::compound_op(const Node& node, const Binding& b) {
    const Node& target = ast_.at(node.a);
    const std::uint16_t mark = fb_->top();
    // CP12b: a global is updated in a register taken from main's slot (after
    // the right-hand side, which may read it) and stored back.
    std::uint16_t reg = b.reg, binding_type = b.type_reg, value = 0;
    const bool indirect = b.cell || b.upvalue;
    Value small;
    const Node* rhs = node.kind == NodeKind::IncDec ? nullptr : &ast_.at(node.b);
    const bool immediate = rhs != nullptr && node.op == Op::AddAssign && rhs->kind == NodeKind::Number &&
                           decode_number_literal(text(*rhs), small) && small.is(Kind::Int) && small.as_int() <= 32767;
#if !(defined(NIFT_D2_MUTANT) && NIFT_D2_MUTANT == 3)  // D2-3: the target is read after the rhs again
    if (rhs != nullptr && !immediate && may_rebind(node.b)) return compound_snapshot(node, b);  // D2 family B
#endif
    if (rhs != nullptr && !immediate && !expr_any(node.b, value)) return false;
    if (indirect) {
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
    if (loop_type_error_ambiguous() && binding_error_possible(b, kinds(node.a))) return fail(node, "context:assignment-error");
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
    if (target.kind == NodeKind::Index) return ElementPath::compound(*this, node);  // checkpoint 1
    if (target.kind != NodeKind::Ident) return fail(target, "compound-target");
    const Binding* binding = find_binding(text(target));
    if (!binding || binding->invocation) return fail(target, write_refusal(binding, text(target), "assign-free-name"));
    if (binding->const_array) return fail(target, "declare-const");
    if (binding->readonly) return fail(target,
        session_ && binding->decl_kinds == kKindFunction ? "session:function-assignment" : "assign-catch-binding");
    if (binding->struct_id >= 0) return fail(target, "struct-assign");
    const Binding b = *binding;
    if (kinds(node.a) & kKindAtomic) return atomic_update(node, b, 0, false);
    // `+=` on two numbers or two strings, the others on numbers (legacy
    // errors and prepared array concatenation are refused).
    const KindSet l = kinds(node.a), r = kinds(node.b);
    const bool numbers = (l & ~kKindNumber) == 0 && (r & ~kKindNumber) == 0;
    const bool strings = node.op == Op::AddAssign && l == kKindString && r == kKindString;
    const bool dynamic = ((l | r) & kKindUnknown) != 0 || l == 0 || r == 0;  // legacy checks the reached value kinds
    if (!numbers && !strings && !dynamic && !unknown_defers(l | r))
        return fail(node, "type:compound-assignment");
    if (loop_reachable()) {
        // Legacy's prepared `%=` computes fmod on any numbers (the canonical
        // rule requires integer-valued operands).
        if (node.op == Op::ModAssign && ((l | r) & (kKindFrac | kKindUnknown)))
            return fail(node, "context:modulo-assignment");
        // Legacy's prepared `s += t` on strings skips the binding type check
        // (execute_prepared appends in place), so a binding typed double that
        // holds a string keeps it there and fails canonically.
        // Only a textual double binding can hold a string under a double
        // binding type (the prepared append skips that check). An unproved
        // binding's tag follows its value, so it needs no refusal.
        if (node.op == Op::AddAssign && ((l | r) & kKindString) && b.decl_text_double)
            return fail(node, "context:string-append-type");
    }
    // CP25: legacy computes the operation (its errors first), then rejects the write.
    if (b.const_scalar) return const_write(node, &ast_.at(node.b), nullptr, b, true);
    return compound_op(node, b);
}

bool UnitCompiler::inc_dec_expr(const Node& node, std::uint16_t dst) {
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(target, "incdec-target");
    const Binding* found = find_binding(text(target));
    if (!found || found->invocation) return fail(target, write_refusal(found, text(target), "assign-free-name"));
    if (found->const_array) return fail(target, "declare-const");
    if (found->readonly) return fail(target, "assign-catch-binding");
    const Binding b = *found;
    if (kinds(node.a) & kKindAtomic) return atomic_update(node, b, dst, true);
    if (!kinds_within(node.a, kKindNumber, node, "type:increment")) return false;
    // CP25: after the numeric check; a loop-code prefix is legacy's unapplied read (below).
    const bool loop_prefix = loop_reachable() && node.op != Op::PostInc && node.op != Op::PostDec;
    if (b.const_scalar && !loop_prefix) return const_write(node, nullptr, &dst, b, true);
    const std::uint16_t mark = fb_->top();
    std::uint16_t work = 0;
    if (!temp(work)) return false;
    if (b.global && !b.cell) {
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
    if (loop_reachable() && !post) {
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
    if (b.cell || b.upvalue) {
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
