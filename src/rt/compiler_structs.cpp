#include "rt/compiler_structs.h"

#include "rt/alias.h"
#include "rt/atom.h"

// CP12b structs (rt/structs.h). Instances are values on the VM (D-040 V9,
// copy-on-write StructObject); legacy's are references. The two agree on
// every unit admitted here because an instance never has two holders: it
// is created by a declaration `p := Name(args)`, lives in that binding, and
// is used only as the receiver of its fields (`p.x`, `p.x = v`) and methods
// (`p.m(args)`); any other use of the binding (copy, argument, return,
// comparison, printing) is refused (`struct-value`). A method gets its
// instance's absolute stack slot as a hidden first argument and reads and
// writes the instance in place there (FieldGet/FieldSet), so `this`,
// implicit fields (`x` inside a method) and nested method calls all reach
// the caller's binding. Field writes keep the field's binding type
// (legacy's "cannot change struct field type"). Legacy's loop evaluator
// fails on field access inside loop bodies (L42): refused there
// (`context:struct-field`); method calls from loops are admitted.

namespace nift::rt::detail {

const StructFieldInfo* UnitCompiler::own_field(std::string_view name) const {
    const int s = current_struct();
    return s < 0 ? nullptr : plan_.structs[static_cast<std::size_t>(s)].field(name);
}

int UnitCompiler::struct_of_receiver(NodeId object, bool& is_this) const {
    is_this = false;
    if (object == kNoNode) return -1;
    const Node& o = ast_.at(object);
    if (o.kind != NodeKind::Ident) return -1;
    if (text(o) == "this" && current_struct() >= 0) {  // rt-lint: allow-string-compare compile time
        is_this = true;
        return current_struct();
    }
    const Binding* b = find_binding(text(o));
    return b != nullptr ? b->struct_id : -1;
}

// The absolute stack slot of the instance `object` (kNoNode: `this`).
bool UnitCompiler::receiver_slot(NodeId object, std::uint16_t dst) {
    bool is_this = false;
    if (object == kNoNode || (struct_of_receiver(object, is_this) >= 0 && is_this)) {
        fb_->emit(OpCode::Move, dst, this_reg_);
        return true;
    }
    const Binding* b = find_binding(text(ast_.at(object)));
    if (b == nullptr || b->struct_id < 0) return fail(ast_.at(object), "struct-value");
    if (!b->global) {
        fb_->emit(OpCode::SlotOf, dst, b->reg);
        return true;
    }
    if (session_ && !global_check_defined(b->reg, b->session_name)) return false;
    // Negative locations identify stable session cells. Ordinary scripts keep
    // their existing absolute main-frame locations, and constructors use a
    // temporary stack location until the instance is published.
    const auto location = session_ ? -1 - static_cast<std::int64_t>(b->reg) : b->reg;
    std::uint16_t k = 0;
    if (!fb_->constant(Value::integer(location), k)) return builder_ok();
    fb_->emit(OpCode::LoadK, dst, static_cast<std::uint16_t>(k & ~kConstantBit));
    return true;
}

bool UnitCompiler::field_access_ok(const Node& at, int s, std::string_view name, bool is_this,
                                   const StructFieldInfo*& field) {
    field = plan_.structs[static_cast<std::size_t>(s)].field(name);
    if (field == nullptr) return fail(at, "struct-no-field");
    if (field->priv && !is_this) return fail(at, "struct-private");  // legacy: only through `this`
    if (field_in_loop_code()) return fail(at, "context:struct-field");  // L42
    return true;
}

bool UnitCompiler::field_read(const Node& at, NodeId receiver, const StructFieldInfo& field, std::uint16_t dst) {
    const std::uint16_t mark = fb_->top();
    std::uint16_t slot = 0;
    if (!temp(slot) || !receiver_slot(receiver, slot)) return false;
    const Atom key = atom(field.name);
    if (key.id > 0xffff) return fail(at, "limit:atoms");
    fb_->set_offset(site(at));
    const std::size_t op = fb_->emit(OpCode::FieldGet, dst, slot, static_cast<std::uint16_t>(key.id));
    fb_->note(op, std::string(field.name));
    fb_->release_to(mark > dst ? mark : static_cast<std::uint16_t>(dst + 1));
    return true;
}

bool UnitCompiler::struct_member(const Node& node, int s, bool is_this, std::uint16_t dst) {
    if (!plain_postfix(node)) return false;
    const StructFieldInfo* field = nullptr;
    if (!field_access_ok(node, s, text(ast_.at(node.b)), is_this, field)) return false;
    return field_read(node, node.a, *field, dst);
}

// `x = e` / `this.x = e` / `p.x = e`.
bool UnitCompiler::field_assign(const Node& node, NodeId receiver, const StructFieldInfo& field) {
    if (!no_alias(node.b, node)) return false;
    std::uint16_t value = 0, type = 0, slot = 0;
    if (!temp(value) || !expr_to(node.b, value)) return false;
    if (!fb_->constant(Value::integer(field.type), type)) return builder_ok();
    if (!temp(slot) || !receiver_slot(receiver, slot)) return false;
    const Atom key = atom(field.name);
    if (key.id > 0xffff) return fail(node, "limit:atoms");
    fb_->set_offset(node.begin);
    const std::uint16_t wording = receiver == kNoNode ? 1 : 0;  // an implicit field: legacy's binding-like wording
    fb_->note(fb_->emit(OpCode::CheckField, value, type, wording), std::string(field.name));
    fb_->note(fb_->emit(OpCode::FieldSet, slot, static_cast<std::uint16_t>(key.id), value), std::string(field.name));
    return true;
}

// `x += e`, `x++` (and through `this.` or an instance).
bool UnitCompiler::field_update(const Node& node, NodeId receiver, const StructFieldInfo& field) {
    const KindSet l = kinds(node.a);
    std::uint16_t value = 0, current = 0, type = 0, slot = 0;
    if (node.kind == NodeKind::IncDec) {
        if (!kinds_within(node.a, kKindNumber, node, "type:increment")) return false;
    } else {
        const KindSet r = kinds(node.b);
        const bool numbers = (l & ~kKindNumber) == 0 && (r & ~kKindNumber) == 0;
        const bool strings = node.op == Op::AddAssign && l == kKindString && r == kKindString;
        if (!numbers && !strings && !unknown_defers(l | r)) return fail(node, "type:compound-assignment");
        if (!expr_any(node.b, value)) return false;
    }
    if (!temp(slot) || !receiver_slot(receiver, slot) || !temp(current)) return false;
    if (!fb_->constant(Value::integer(field.type), type)) return builder_ok();
    const Atom key = atom(field.name);
    if (key.id > 0xffff) return fail(node, "limit:atoms");
    fb_->set_offset(node.begin);
    fb_->note(fb_->emit(OpCode::FieldGet, current, slot, static_cast<std::uint16_t>(key.id)), std::string(field.name));
    if (node.kind == NodeKind::IncDec) {
        const bool up = node.op == Op::PreInc || node.op == Op::PostInc;
        fb_->emit(OpCode::AddI, current, current, jump_operand(up ? 1 : -1));
    } else {
        switch (node.op) {
            case Op::AddAssign: fb_->emit(OpCode::AddAssign, current, value); break;
            case Op::SubAssign: fb_->emit(OpCode::Sub, current, current, value); break;
            case Op::MulAssign: fb_->emit(OpCode::Mul, current, current, value); break;
            case Op::DivAssign: fb_->emit(OpCode::Div, current, current, value); break;
            case Op::ModAssign: fb_->emit(OpCode::Mod, current, current, value); break;
            default: return fail(node, "compound-operator");
        }
    }
    const std::uint16_t wording = receiver == kNoNode && node.kind != NodeKind::IncDec ? 2 : 0;
    fb_->note(fb_->emit(OpCode::CheckField, current, type, wording), std::string(field.name));
    fb_->note(fb_->emit(OpCode::FieldSet, slot, static_cast<std::uint16_t>(key.id), current), std::string(field.name));
    return builder_ok();
}

const StructFieldInfo* UnitCompiler::struct_target(const Node& target) {
    const StructFieldInfo* field = nullptr;
    if (target.kind == NodeKind::Member) {
        bool is_this = false;
        const int s = struct_of_receiver(target.a, is_this);
        if (s >= 0 && !field_access_ok(target, s, text(ast_.at(target.b)), is_this, field)) return nullptr;
        return field;
    }
    if (target.kind != NodeKind::Ident || find_binding(text(target)) != nullptr) return nullptr;
    field = own_field(text(target));
    if (field != nullptr && field_in_loop_code()) {
        fail(target, "context:struct-field");
        return nullptr;
    }
    return field;
}

bool UnitCompiler::struct_method(const Node& node, int s, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    const StructMethodInfo* m = plan_.structs[static_cast<std::size_t>(s)].method(text(ast_.at(callee.b)));
    if (m == nullptr) return fail(callee, "struct-no-method");
    return struct_call(node, s, *m, callee.a, -1, dst);
}

int UnitCompiler::struct_callee(const Node& node, std::string_view name, std::uint16_t dst) {
    if (plan_.struct_index.count(name)) return fail(ast_.at(node.a), "struct-construction") ? 1 : 0;
    const int s = current_struct();
    if (s < 0 || plan_.index.count(name)) return -1;
    const StructMethodInfo* m = plan_.structs[static_cast<std::size_t>(s)].method(name);
    if (m == nullptr) return -1;
    return struct_call(node, s, *m, kNoNode, -1, dst) ? 1 : 0;
}

// `p := Name(args)`: the instance in `reg`, fields from their initialisers,
// then the constructor (if any) on it.
bool UnitCompiler::struct_construct(const Node& decl, int s, std::uint16_t reg) {
    const StructInfo& info = plan_.structs[static_cast<std::size_t>(s)];
    const Node& call = ast_.at(decl.b);
    if (loop_region()) return fail(call, "context:struct-new");
    if (!plain_postfix(call) || !no_trailing_comma(call)) return false;
    auto display_name = info.name;
    if (session_) {
        const auto schema = session_->schema_templates.find(std::string(info.name));
        if (schema != session_->schema_templates.end()) display_name = schema->second->name;
    }
    const Atom type = atom(display_name);
    if (type.id > 0xffff) return fail(call, "limit:atoms");
    fb_->set_offset(site(call));
    fb_->emit(OpCode::NewStruct, reg, static_cast<std::uint16_t>(type.id));
    for (const StructFieldInfo& field : info.fields) {
        std::uint16_t k = 0;
        const Atom key = atom(field.name);
        if (key.id > 0xffff) return fail(call, "limit:atoms");
        if (!fb_->constant(field.value, k)) return builder_ok();
        fb_->emit(OpCode::StructAdd, reg, static_cast<std::uint16_t>(key.id), k);
    }
    if (info.ctor < 0) {
        if (call.count != 0) return fail(call, "static-error:struct-arity");
        return true;
    }
    const std::uint16_t mark = fb_->top();
    std::uint16_t result = 0;
    if (!temp(result)) return false;
    const bool ok = struct_call(call, s, info.methods[static_cast<std::size_t>(info.ctor)], kNoNode, reg, result);
    fb_->release_to(mark);
    return ok;
}

// A method call: the instance's slot, then the arguments, in a fresh window.
bool UnitCompiler::struct_call(const Node& node, int s, const StructMethodInfo& m, NodeId receiver,
                               int receiver_reg, std::uint16_t dst) {
    (void)s;
    bool is_this = receiver == kNoNode && receiver_reg < 0;
    if (receiver != kNoNode) struct_of_receiver(receiver, is_this);
    if (m.priv && !is_this) return fail(node, "struct-private");
    if (!plain_postfix(node) || !no_trailing_comma(node)) return false;
    const bool numeric_return = session_ && (fb_->flags() & kInstrReturnCall);
    const std::size_t f = m.function;
    const Node& fn = ast_.at(plan_.functions[f]);
    if (node.count != fn.count) return fail(node, "static-error:arity");
    const NodeId* args = ast_.list(node);
    const std::uint16_t base = fb_->top();
    if (!operand_window(base, node.count + 1)) return false;
    if (receiver_reg >= 0) fb_->emit(OpCode::SlotOf, base, static_cast<std::uint16_t>(receiver_reg));
    else if (!receiver_slot(receiver, base)) return false;
    ++call_args_;
    const NodeId saved_path = path_arg_, saved_chain = path_chain_;
    path_arg_ = path_chain_ = kNoNode;  // no member or index path in method arguments
    bool ok = true;
    for (std::uint32_t k = 0; ok && k < node.count; ++k) {
        if (ast_.at(args[k]).kind == NodeKind::Spread) ok = fail(ast_.at(args[k]), "spread");
        else if (is_place_argument(ast_, args[k]) && f < plan_.escaping.size() && k < plan_.escaping[f].size() &&
                 plan_.escaping[f][k])
            ok = fail(ast_.at(args[k]), "alias-argument");
        else ok = window_value(args[k], static_cast<std::uint16_t>(base + k + 1));
    }
    path_arg_ = saved_path;
    path_chain_ = saved_chain;
    --call_args_;
    if (!ok) return false;
    if (loop_reachable())
        for (std::uint32_t k = 0; k < node.count; ++k)
            if (observable_[f][k] && type_mismatch_possible(text_type_at(args[k]), kinds(args[k]), scope_index()))
                return fail(ast_.at(args[k]), "context:parameter-type");
    if (!pass_param_types(f, node.count, args, static_cast<std::uint16_t>(base + 1))) return false;
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::Call, base, static_cast<std::uint16_t>(f + 1),
                                     static_cast<std::uint16_t>(node.count + 1));
    fb_->note(at, std::string(m.name), false, false, numeric_return);
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(dst >= base ? static_cast<std::uint16_t>(dst + 1) : base);
    return builder_ok();
}

} // namespace nift::rt::detail
