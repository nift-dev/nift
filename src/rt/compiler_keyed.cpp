#include "rt/compiler_expr.h"
#include "rt/builtin_bind.h"
#include "rt/collection.h"
#include "rt/keyed.h"
// rt-lint: cold-file compile-time map/set/stack method lowering (D-063)
namespace nift::rt::detail {
namespace {
// The mutator of `name` on a receiver of exactly `kinds`, or -1.
int keyed_mutator(KindSet kinds, std::string_view name) {
    using K = KeyedMethod;
    if (name == "clear") return static_cast<int>(K::Clear);
    if (kinds == kKindMap) {
        if (name == "set") return static_cast<int>(K::MapSet);
        if (name == "remove") return static_cast<int>(K::MapRemove);
    } else if (kinds == kKindSet) {
        if (name == "add") return static_cast<int>(K::SetAdd);
        if (name == "remove") return static_cast<int>(K::SetRemove);
    } else if (kinds == kKindStack) {
        if (name == "push") return static_cast<int>(K::StackPush);
        if (name == "pop") return static_cast<int>(K::StackPop);
    }
    return -1;
}
// A fallible read compiled as KeyedMutate (rt/keyed.h), or -1.
int keyed_read(KindSet kinds, std::string_view name) {
    using K = KeyedMethod;
    if (kinds == kKindMap && name == "get") return static_cast<int>(K::MapGet);
    if (kinds == kKindSet && name == "contains") return static_cast<int>(K::SetContains);
    if (kinds == kKindStack && name == "top") return static_cast<int>(K::StackTop);
    if (kinds == kKindStack && name == "front") return static_cast<int>(K::StackFront);
    return -1;
}
// Legacy's arity of a map/set/stack method and its arity message, or -1.
int keyed_arity_of(KindSet kinds, std::string_view name, std::string& message) {
    const std::string n(name);
    const bool map = kinds == kKindMap, set = kinds == kKindSet, stack = kinds == kKindStack;
    if (name == "size" || name == "empty" || name == "clear" || (stack && (name == "pop" || name == "top" ||
                                                                         name == "front"))) {
        message = n + ": expected no arguments";
        return 0;
    }
    if (map && name == "set") { message = "set: expected key and value"; return 2; }
    if (map && (name == "get" || name == "contains" || name == "remove")) { message = n + ": expected one key"; return 1; }
    if ((set && (name == "add" || name == "contains" || name == "remove")) || (stack && name == "contains")) {
        message = n + ": expected one value";
        return 1;
    }
    if (stack && name == "push") { message = "push: expected one value"; return 1; }
    return -1;
}
natives::Receiver keyed_receiver(KindSet kinds) {
    return kinds == kKindMap ? natives::Receiver::Map : kinds == kKindSet ? natives::Receiver::Set
                                                                          : natives::Receiver::Stack;
}
} // namespace

// D-063: is `node` a call of a map/set/stack mutator (`m.set(k, v)`,
// `s.add(v)`, `st.push(v)`, `.remove`, `.pop`, `.clear`)?
bool UnitCompiler::keyed_mutator_call(const Node& node) {
    if (node.kind != NodeKind::Call || ast_.at(node.a).kind != NodeKind::Member) return false;
    const Node& callee = ast_.at(node.a);
    return keyed_mutator(kinds(callee.a), text(ast_.at(callee.b))) >= 0 && (kinds(callee.a) & kKindCollection);
}

// D-063: methods of map/set/stack values. Read-only methods are natives over
// the receiver value; mutators are lowered onto the bare binding (legacy
// accepts collection methods on a bare binding only). Callback methods
// (`m.map(...)` etc.) and every other name stay refused.
int UnitCompiler::keyed_method_call(const Node& node, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    const KindSet k = kinds(callee.a);
    // Only a receiver that is certainly a collection: a mixed or "any" value
    // keeps the existing method refusals (builtin-receiver, method:...).
    if (!(k & kKindCollection) || (k & ~kKindCollection)) return -1;
    const std::string name(text(ast_.at(callee.b)));
    if (k != kKindMap && k != kKindSet && k != kKindStack)
        return fail(callee, "collection-receiver:" + name) ? 1 : 0;  // a value that may be of several kinds
    if (collection_method(name) >= static_cast<int>(CollectionMethod::Map))
        return fail(node, "collection-callback:" + name) ? 1 : 0;  // m.map(...), s.filter(...), ...
    const bool presentation = name == "stringify" || name == "prettify" || name == "highlight";
    // Legacy dispatches collection methods only on a bare binding's name
    // (`o["m"].size()`, `f().get(1)` fail there): the same is refused here.
    if (!presentation && (ast_.at(callee.a).kind != NodeKind::Ident || !find_binding(text(ast_.at(callee.a)))))
        return fail(node, "method:" + name) ? 1 : 0;
    // Legacy checks the arity first, before the receiver binding or any
    // argument; its message is a fatal Error. Its loop-code report is not
    // established: refused there.
    std::string arity_message;
    const int arity = keyed_arity_of(k, name, arity_message);
    if (arity >= 0 && node.count != static_cast<std::uint32_t>(arity)) {
        if (loop_reachable() || !plain_postfix(node)) return fail(node, "builtin-arity:" + name) ? 1 : 0;
        return collection_failure(node, dst, std::move(arity_message)) ? 1 : 0;
    }
    if (const int m = keyed_mutator(k, name); m >= 0) return keyed_mutation(node, dst, m) ? 1 : 0;
    if (const int m = keyed_read(k, name); m >= 0) return keyed_mutation(node, dst, m) ? 1 : 0;
    if (k == kKindMap && name == "contains" && node.count == 1 &&
        (kinds(ast_.list(node)[0]) & (kKindObject | kKindHandle | kKindShared | kKindTask)))
        return fail(ast_.at(ast_.list(node)[0]), "collection-identity-key") ? 1 : 0;
    const natives::BuiltinSpec* spec = natives::find_builtin(keyed_receiver(k), name);
    if (!spec && presentation)
        spec = natives::find_builtin(natives::Receiver::Any, name);
    if (!spec) return fail(callee, "method:" + name) ? 1 : 0;
    return builtin_call(node, *spec, callee.a, dst) ? 1 : 0;
}

// A key or member argument legacy accepts as a tagged string with identity
// (a struct, a host or shared handle, a task) is not compiled: this VM
// compares structs as values and holds handles as typed values. Keys of
// other kinds behave alike (bool/number/string, collections structurally,
// callables by identity; anything else is legacy's run-time error).
bool UnitCompiler::keyed_key_ok(const Node& node, int id) {
    using K = KeyedMethod;
    const auto m = static_cast<K>(id);
    const bool key = m == K::MapSet || m == K::MapRemove || m == K::SetAdd || m == K::SetRemove ||
                     m == K::MapGet || m == K::SetContains;
    if (!key || node.count == 0) return true;
    const NodeId arg = ast_.list(node)[0];
    if (kinds(arg) & (kKindObject | kKindHandle | kKindShared | kKindTask))
        return fail(ast_.at(arg), "collection-identity-key");
    return true;
}

// A fallible read: the receiver is evaluated as a value (a copy), then the
// argument; KeyedMutate reads and nothing is put back.
bool UnitCompiler::keyed_read_call(const Node& node, std::uint16_t dst, int id) {
    using namespace collection_slot;
    const NodeId* args = ast_.list(node);
    if (!keyed_key_ok(node, id)) return false;
    for (std::uint32_t k = 0; k < node.count; ++k)
        if (ast_.at(args[k]).kind == NodeKind::Spread) return fail(ast_.at(args[k]), "spread");
    const std::uint16_t mark = fb_->top(), state = mark;
    for (std::uint16_t k = 0; k < Count; ++k) {
        std::uint16_t reg = 0;
        if (!temp(reg)) return false;
        fb_->emit(OpCode::LoadNull, reg);
    }
    if (!expr_to(ast_.at(node.a).a, state + Source)) return false;
    if (node.count > 0 && !expr_to(args[0], state + Item)) return false;
    fb_->set_offset(collection_offset(node));
    fb_->emit(OpCode::KeyedMutate, state, static_cast<std::uint16_t>(id), 0);
    fb_->emit(OpCode::Move, dst, state + Result);
    for (std::uint16_t k = 0; k < Count; ++k) fb_->emit(OpCode::LoadNull, state + k);
    fb_->release_to(mark);
    return builder_ok();
}

// Same take / mutate / put lowering as UnitCompiler::array_mutation: the
// arguments are evaluated first (so `m.set(1, m)` stores the receiver's value
// before the write: a snapshot, never a cycle), then the collection is moved
// out of its binding (an unshared collection is mutated in place; a shared
// one is cloned by the value layer), mutated, and put back.
bool UnitCompiler::keyed_mutation(const Node& node, std::uint16_t dst, int id) {
    using namespace collection_slot;
    const auto method = static_cast<KeyedMethod>(id);
    const std::string name(keyed_names[id]);
    const Node& receiver = ast_.at(ast_.at(node.a).a);
    if (receiver.kind != NodeKind::Ident) return fail(node, "method:" + name);  // legacy: bare bindings only
    if (method >= kFirstKeyedRead) return keyed_read_call(node, dst, id);
    const Binding* found = find_binding(text(receiver));
    if (!found) return fail(node, "method:" + name);
    const Binding binding = *found;
    // Bounded subset: const collections and loop/selector bindings are refused
    // (legacy's const check order and loop locations are not modelled).
    if (binding.const_array || binding.const_scalar || binding.readonly) return fail(node, "collection-const");
    if (binding.selector || binding.loop_binding || binding.lazy_for || binding.lazy_pair)
        return fail(node, "collection-loop-binding");
    if (node.count != keyed_arity[id]) return fail(node, "builtin-arity:" + name);  // checked by the caller
    const NodeId* args = ast_.list(node);
    if (!keyed_key_ok(node, id)) return false;
    for (std::uint32_t k = 0; k < node.count; ++k)
        if (ast_.at(args[k]).kind == NodeKind::Spread) return fail(ast_.at(args[k]), "spread");
    const std::uint16_t mark = fb_->top(), state = mark;
    for (std::uint16_t k = 0; k < Count; ++k) {
        std::uint16_t reg = 0;
        if (!temp(reg)) return false;
        fb_->emit(OpCode::LoadNull, reg);
    }
    auto read = [&]() -> bool {
        std::uint16_t value = 0;
        if (binding.global && !binding.cell) {
            if (!global_take(binding, value, text(receiver))) return false;
        } else if (binding.cell || binding.upvalue) {
            if (!indexed_binding(binding, value)) return false;
            std::uint16_t cell = 0;
            if (!binding_cell(binding, cell)) return false;
            fb_->emit(OpCode::CellTake, value, cell);
        } else {
            value = binding.reg;
        }
        fb_->emit(OpCode::Move, state + Source, value);
        fb_->emit(OpCode::LoadNull, value);
        return true;
    };
    auto put = [&]() -> bool {
        if (binding.global) global_put(binding, state + Source);
        else if (binding.cell || binding.upvalue) {
            if (!store_indexed_binding(binding, state + Source)) return false;
        } else fb_->emit(OpCode::Move, binding.reg, state + Source);
        fb_->emit(OpCode::LoadNull, state + Source);
        return true;
    };
#if defined(NIFT_COLLECTIONS_MUTANT) && NIFT_COLLECTIONS_MUTANT == 3
    if (!read()) return false;  // M3: the binding is taken before the arguments run (no snapshot)
#endif
    if (node.count > 0 && !expr_to(args[0], state + Item)) return false;
    if (node.count > 1 && !expr_to(args[1], state + Returned)) return false;
#if !(defined(NIFT_COLLECTIONS_MUTANT) && NIFT_COLLECTIONS_MUTANT == 3)
    if (!read()) return false;
#endif
    fb_->set_offset(collection_offset(node));
    fb_->emit(OpCode::KeyedMutate, state, static_cast<std::uint16_t>(id), 0);
#if defined(NIFT_COLLECTIONS_MUTANT) && NIFT_COLLECTIONS_MUTANT == 4
    fb_->emit(OpCode::LoadNull, state + Source);  // M4: the mutated value is never put back
#endif
    if (!put()) return false;
    static_cast<void>(method);
    fb_->emit(OpCode::Move, dst, state + Result);
    for (std::uint16_t k = 0; k < Count; ++k) fb_->emit(OpCode::LoadNull, state + k);
    fb_->release_to(mark);
    return builder_ok();
}
} // namespace nift::rt::detail
