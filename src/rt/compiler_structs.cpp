#include "rt/compiler_structs.h"
#include "rt/compiler_presentation.h"
#include "rt/aggregate.h"
#include "rt/alias.h"
#include "rt/atom.h"
#include "rt/natives/builtin.h"
#include "rt/native.h"
// CP12b structs (rt/structs.h). Struct instances are identity-bearing mutable
// references (D-NEXT-refsem #1, superseding D-046 #3 / D-040 V9): legacy's
// model. A binding, parameter, return value, container element or captured
// variable holding an instance holds the same object, and a field write
// through any of them is visible through all (FieldSet mutates the shared
// StructObject in place). A receiver (`p.x`, `p.x = v`, `p.m(args)`) is a
// bare name whose struct is known statically: the declaration `p := Name()`
// or the kind inference (UnitKinds::struct_ids, exactly one struct type and
// nothing else); a method gets its instance as a hidden first argument
// (`this`). Field writes keep the field's binding type (legacy's "cannot
// change struct field type") and are refused before any change when they
// would close a reference cycle (legacy's "assignment would create a cyclic
// reference: <field>"). Legacy's loop evaluator fails on field access inside
// loop bodies (L42): refused there (`context:struct-field`); method calls
// from loops are admitted.

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
    if (b == nullptr) return -1;
    if (b->struct_id >= 0) return b->struct_id;  // `p := Name(args)` (never rebound: struct-assign)
    return inferred_struct(object);
}

// D-NEXT-refsem: the struct of an instance-valued expression, when the kind
// inference proves it is always an instance of exactly that struct.
int UnitCompiler::inferred_struct(NodeId id) const {
    if (kinds(id) != kKindStruct) return -1;
    const auto found = kinds_.struct_ids.find(id);
    return found != kinds_.struct_ids.end() && found->second >= 0 ? found->second : -1;
}

// The instance `object` (kNoNode: `this`) into `dst`: a reference to the
// shared StructObject (D-NEXT-refsem #1).
bool UnitCompiler::receiver_value(NodeId object, std::uint16_t dst) {
    bool is_this = false;
    if (object == kNoNode || (struct_of_receiver(object, is_this) >= 0 && is_this)) {
        fb_->emit(OpCode::Move, dst, this_reg_);
        return true;
    }
    if (struct_of_receiver(object, is_this) < 0) return fail(ast_.at(object), "struct-value");
    return expr_to(object, dst);
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
    if (!temp(slot) || !receiver_value(receiver, slot)) return false;
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
    std::uint16_t value = 0, type = 0, slot = 0;
    if (!temp(value) || !expr_to(node.b, value)) return false;
    if (!fb_->constant(Value::integer(field.type), type)) return builder_ok();
    if (!temp(slot) || !receiver_value(receiver, slot)) return false;
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
#if defined(NIFT_D2_MUTANT) && NIFT_D2_MUTANT == 6
        if (!expr_any(node.b, value)) return false;  // D2-6: the field is read after the rhs
#endif
    }
    // D2 family C: the receiver and the field are read before the rhs is evaluated.
    if (!temp(slot) || !receiver_value(receiver, slot) || !temp(current)) return false;
    if (!fb_->constant(Value::integer(field.type), type)) return builder_ok();
    const Atom key = atom(field.name);
    if (key.id > 0xffff) return fail(node, "limit:atoms");
    fb_->set_offset(node.begin);
    fb_->note(fb_->emit(OpCode::FieldGet, current, slot, static_cast<std::uint16_t>(key.id)), std::string(field.name));
#if !(defined(NIFT_D2_MUTANT) && NIFT_D2_MUTANT == 6)
    if (node.kind != NodeKind::IncDec) {
        if (!expr_any(node.b, value)) return false;
        fb_->set_offset(node.begin);
    }
#endif
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
    // B3 (owner ruling): an explicit `p.stringify()` / `p.prettify()` /
    // `p.highlight()` calls the struct's own method of that name when it
    // declares one (below; legacy ignores it: an approved difference), else the
    // presentation built-in renders the instance (legacy parity). Only these
    // three names: the rest of method dispatch is B4's.
    const std::string_view name = text(ast_.at(callee.b));
    const StructInfo& info = plan_.structs[static_cast<std::size_t>(s)];
    bool is_this = false;  // B4 N3: the resolver's `this`, not the receiver's text
    struct_of_receiver(callee.a, is_this);
    if (presentation_builtin_binds(info, name, node.count, is_this)) {
        if (!plain_postfix(node) || !plain_postfix(callee) || !no_trailing_comma(node)) return false;
        if (const natives::BuiltinSpec* spec = natives::find_builtin(natives::Receiver::Any, name))
            return builtin_call(node, *spec, callee.a, dst);
    }
    const StructMethodInfo* m = plan_.structs[static_cast<std::size_t>(s)].method(text(ast_.at(callee.b)));
    // B4 (review H1): legacy reports a method the struct does not declare as an
    // ordinary error at the call, before evaluating any argument ("struct has
    // no method: <name>"), except for a string method's name: its `substr`
    // anywhere, and string methods in prepared code, run on the instance's tag
    // text. Compiled as that error only for other names outside prepared code
    // (b4-dispatch.md 2); otherwise refused as in B3.
#if !(defined(NIFT_B4_MUTANT) && NIFT_B4_MUTANT == 10)
    const bool string_name = natives::find_builtin(natives::Receiver::String, name) != nullptr;
#if defined(NIFT_B4_MUTANT) && NIFT_B4_MUTANT == 15
    const bool legacy_fails = true || string_name;  // A15: every undeclared name compiled as the error (H1)
#else
    const bool legacy_fails = !string_name && !loop_reachable();
#endif
    if (m == nullptr && !legacy_fails) return fail(callee, "struct-no-method");
    if (m == nullptr)
        return plain_postfix(node) && legacy_fatal(node, "struct has no method: " + std::string(name), dst);
#else
    if (m == nullptr) return fail(callee, "struct-no-method");  // A10: the old refusal (precision lost)
#endif
    return struct_call(node, s, *m, callee.a, -1, dst);
}

// `Name(args)` in an expression (D-NEXT-refsem: a new instance anywhere a
// value is, e.g. `return P()`, `f(P())`, `[P()]`).
int UnitCompiler::struct_callee(const Node& node, std::string_view name, std::uint16_t dst) {
    if (const auto made = plan_.struct_index.find(name); made != plan_.struct_index.end())
        return struct_construct(node, static_cast<int>(made->second), dst) ? 1 : 0;
    const int s = current_struct();
    if (s < 0 || plan_.index.count(name)) return -1;
    const StructMethodInfo* m = plan_.structs[static_cast<std::size_t>(s)].method(name);
    if (m == nullptr) return -1;
    return struct_call(node, s, *m, kNoNode, -1, dst) ? 1 : 0;
}

// B3 (b3-structs.md): one descriptor per declaration per compile; in a
// session one per schema version: the cached descriptor is reused while the
// imported SessionDefinition is the one it was made for, and a replaced schema
// gets a new descriptor (instances built earlier keep theirs).
std::shared_ptr<const StructType> struct_descriptor(const UnitPlan& plan, SessionCompileContext* session,
                                                    std::vector<std::shared_ptr<const StructType>>& cache, int s,
                                                    const std::string& display_name) {
    const auto index = static_cast<std::size_t>(s);
    if (cache.size() < plan.structs.size()) cache.resize(plan.structs.size());
    if (cache[index]) return cache[index];
    const StructInfo& info = plan.structs[index];
    std::vector<std::string> names;
    std::vector<bool> privacy;
    for (const StructFieldInfo& field : info.fields) {
        names.emplace_back(field.name);
        privacy.push_back(field.priv);
    }
    std::shared_ptr<const StructType> made;
    if (session) {
        const auto schema = session->schema_templates.find(std::string(info.name));
        const std::shared_ptr<const SessionDefinition> definition =
            schema != session->schema_templates.end() ? schema->second : nullptr;
        SessionStructType& cached = session->struct_types[std::string(info.name)];
        if (definition && cached.type && cached.definition == definition) {
            made = cached.type;  // the same schema version as an earlier statement
        } else {
            made = StructType::make(display_name, std::move(names), std::move(privacy));
#if defined(NIFT_B3_MUTANT) && NIFT_B3_MUTANT == 3
            // D3: a replaced schema rewrites the cached descriptor in place, so
            // instances built under the old schema render with the new one.
            if (cached.type && definition != cached.definition) {
                const_cast<StructType&>(*cached.type) = *made;
                made = cached.type;
            }
#endif
            cached.definition = definition;  // null: declared by this statement (bound at publication)
            cached.type = made;
        }
    } else {
        made = StructType::make(display_name, std::move(names), std::move(privacy));
    }
    cache[index] = made;
    return made;
}

// `Name(args)`: the instance in `reg`, fields from their initialisers, then
// the constructor (if any) on it. A map()/set()/stack() initialiser is
// evaluated afresh for every instance (D-NEXT-refsem #1: no two instances
// ever share one collection), like legacy's per-construction evaluation.
bool UnitCompiler::struct_construct(const Node& call, int s, std::uint16_t reg) {
    const StructInfo& info = plan_.structs[static_cast<std::size_t>(s)];
    if (loop_region()) return fail(call, "context:struct-new");
    if (!plain_postfix(call) || !no_trailing_comma(call)) return false;
    std::string display_name(info.name);
    if (session_) {
        const auto schema = session_->schema_templates.find(display_name);
        if (schema != session_->schema_templates.end()) display_name = schema->second->name;
    }
    std::uint16_t type = 0;
    const auto descriptor = struct_descriptor(plan_, session_, struct_descriptors_, s, display_name);
    if (!fb_->struct_type(descriptor, type)) return builder_ok();
    const std::uint16_t mark = fb_->top();
    std::uint16_t fresh = 0;
    if (!temp(fresh)) return false;
    fb_->set_offset(site(call));
    fb_->emit(OpCode::NewStruct, fresh, type);
    for (const StructFieldInfo& field : info.fields) {
        const Atom key = atom(field.name);
        if (key.id > 0xffff) return fail(call, "limit:atoms");
        if (field.value.is(Kind::Collection)) {
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 6
            std::uint16_t k = 0;  // R6: the initialiser is one cached constant shared by every instance
            if (!fb_->constant(field.value, k)) return builder_ok();
            fb_->emit(OpCode::StructAdd, fresh, static_cast<std::uint16_t>(key.id), k);
            continue;
#else
            using natives::BuiltinId;
            const CollectionKind kind = collection_kind(field.value);
            const BuiltinId make = kind == CollectionKind::Map   ? BuiltinId::FnMap
                                   : kind == CollectionKind::Set ? BuiltinId::FnSet : BuiltinId::FnStack;
            std::uint16_t value = 0;
            if (!temp(value)) return false;
            const auto native = static_cast<std::uint16_t>(kFirstBuiltin + static_cast<std::uint16_t>(make));
            fb_->emit(OpCode::CallNative, value, native, 0);
            fb_->emit(OpCode::StructAdd, fresh, static_cast<std::uint16_t>(key.id), value);
            fb_->emit(OpCode::LoadNull, value);
            continue;
#endif
        }
        std::uint16_t k = 0;
        if (!fb_->constant(field.value, k)) return builder_ok();
        fb_->emit(OpCode::StructAdd, fresh, static_cast<std::uint16_t>(key.id), k);
    }
    if (info.ctor < 0) {
        if (call.count != 0) return fail(call, "static-error:struct-arity");
        fb_->emit(OpCode::Move, reg, fresh);
        fb_->release_to(mark > reg ? mark : static_cast<std::uint16_t>(reg + 1));
        return builder_ok();
    }
    std::uint16_t result = 0;
    if (!temp(result)) return false;
    if (!struct_call(call, s, info.methods[static_cast<std::size_t>(info.ctor)], kNoNode, fresh, result))
        return false;
    fb_->emit(OpCode::Move, reg, fresh);
    fb_->release_to(mark > reg ? mark : static_cast<std::uint16_t>(reg + 1));
    return builder_ok();
}

} // namespace nift::rt::detail
