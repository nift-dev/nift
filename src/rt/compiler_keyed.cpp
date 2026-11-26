#include "rt/compiler_keyed.h"

#include "rt/compiler_expr.h"
#include "rt/builtin_bind.h"
#include "rt/collection.h"
#include "rt/keyed.h"
// rt-lint: cold-file compile-time map/set/stack method lowering (D-063)
namespace nift::rt::detail {
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
    if (map && (name == "get" || name == "contains" || name == "remove")) {
        message = n + ": expected one key";
        return 1;
    }
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
    // B4 (b4-dispatch.md 2): legacy's prepared evaluator (loop code and the
    // functions called from it) answers false for an empty collection's
    // empty(); the canonical one, true. Refused there before any effect.
    if (name == "empty" && loop_reachable())  // rt-lint: allow-string-compare compile time
        return fail(node, "context:collection-empty") ? 1 : 0;
    if (k != kKindMap && k != kKindSet && k != kKindStack) {
        // B4: a bare binding of several collection kinds dispatches the
        // read-only natives every family has (size, empty) at run time.
        const Node& r = ast_.at(callee.a);
        if (r.kind == NodeKind::Ident && find_binding(text(r)))
            if (const int d = dynamic_method_call(node, callee.a, name, dst); d >= 0) return d;
        return fail(callee, "collection-receiver:" + name) ? 1 : 0;  // a value that may be of several kinds
    }
    if (collection_method(name) >= static_cast<int>(CollectionMethod::Map))
        return fail(node, "collection-callback:" + name) ? 1 : 0;  // m.map(...), s.filter(...), ...
    const bool presentation = name == "stringify" || name == "prettify" || name == "highlight";
    // Legacy dispatches collection methods only on a bare binding's name
    // (`o["m"].size()`, `f().get(1)` fail there): the same is refused here.
    if (!presentation && (ast_.at(callee.a).kind != NodeKind::Ident || !find_binding(text(ast_.at(callee.a)))))
        return fail(node, "method:" + name) ? 1 : 0;
    // Textual dispatch checks arity before evaluating arguments. Prepared
    // dispatch can evaluate them, so only lower the verified literal cases
    // there. Effectful or faulting arguments still require that evaluation.
    std::string arity_message;
    const int arity = keyed_arity_of(k, name, arity_message);
    if (arity >= 0 && node.count != static_cast<std::uint32_t>(arity)) {
        if (loop_reachable()) {
            if (k != kKindMap || (name != "contains" && name != "set") ||
                (fb_->flags() & kInstrReturnCall))
                return fail(node, "builtin-arity:" + name) ? 1 : 0;
            for (std::uint32_t i = 0; i < node.count; ++i) {
                const Node& arg = ast_.at(ast_.list(node)[i]);
                if (arg.kind != NodeKind::String || arg.count != 0)
                    return fail(node, "builtin-arity:" + name) ? 1 : 0;
                const auto spelling = text(arg);
                if (spelling.size() < 2 || spelling.front() != '"' || spelling.back() != '"' ||
                    spelling.substr(1, spelling.size() - 2).find_first_not_of(
                        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string_view::npos)
                    return fail(node, "builtin-arity:" + name) ? 1 : 0;
            }
        }
        if (!plain_postfix(node)) return fail(node, "builtin-arity:" + name) ? 1 : 0;
        return collection_failure(node, dst, std::move(arity_message)) ? 1 : 0;
    }
    if (const int m = keyed_mutator(k, name); m >= 0) return keyed_mutation(node, dst, m) ? 1 : 0;
    if (const int m = keyed_read(k, name); m >= 0) return keyed_mutation(node, dst, m) ? 1 : 0;
    if (k == kKindMap && name == "contains" && node.count == 1 && !key_kinds_ok(ast_.list(node)[0]))
        return 0;
    const natives::BuiltinSpec* spec = natives::find_builtin(keyed_receiver(k), name);
    if (!spec && presentation)
        spec = natives::find_builtin(natives::Receiver::Any, name);
    if (!spec) return fail(callee, "method:" + name) ? 1 : 0;
    if (presentation)  // B3: a struct's own presentation method (compiler_structs.cpp)
        if (const int dispatched = presentation_dispatch(node, k, name, dst); dispatched >= 0) return dispatched;
    return builtin_call(node, *spec, callee.a, dst) ? 1 : 0;
}

// A key or member argument legacy accepts as a tagged string with identity
// (a struct, a host or shared handle, a task) is not compiled: struct identity
// keys are not established on the VM (D-NEXT-refsem #6 keeps the D-063
// refusal) and handles are typed values here. D-NEXT-refsem #6: a mutable
// collection (a shared reference whose contents may change after insertion,
// which a hash index cannot follow) is refused as a key or member before any
// effect, for lookups too; JavaScript-style identity keys are a future
// design question. Ordinary keys (bool, number, string) and callables (by
// identity) are unaffected; anything else is legacy's run-time error.
bool UnitCompiler::key_kinds_ok(NodeId arg) {
    const KindSet k = kinds(arg);
#if !(defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 11)
    if (k & kKindCollection) return fail(ast_.at(arg), "collection-mutable-key");
#endif
    // An object is a value legacy rejects as a key at run time; it is
    // refused here under its own reason (review of ad9d842f: it was reported
    // as "a struct or handle").
    if (k & kKindObject) return fail(ast_.at(arg), "collection-object-key");
    if (k & (kKindStruct | kKindHandle | kKindShared | kKindTask))
        return fail(ast_.at(arg), "collection-identity-key");
    return true;
}

bool UnitCompiler::keyed_key_ok(const Node& node, int id) {
    using K = KeyedMethod;
    const auto m = static_cast<K>(id);
    const bool key = m == K::MapSet || m == K::MapRemove || m == K::SetAdd || m == K::SetRemove ||
                     m == K::MapGet || m == K::SetContains;
    if (!key || node.count == 0) return true;
    return key_kinds_ok(ast_.list(node)[0]);
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

// D-NEXT-refsem #1, H-A (combined-b3-b5.md): the receiver binding is read first
// (a reference to the shared collection, retained in state+Source while the
// arguments run, so an argument that rebinds the name does not redirect the
// mutation; legacy's canonical order), then the arguments are evaluated.
// Nothing is taken out of the binding, so a catchable error such as a refused
// reference cycle leaves every binding as it was, and KeyedMutate changes the
// collection in place, visibly through every alias.
bool UnitCompiler::keyed_mutation(const Node& node, std::uint16_t dst, int id) {
    using namespace collection_slot;
    const std::string name(keyed_names[id]);
    const Node& receiver = ast_.at(ast_.at(node.a).a);
    if (receiver.kind != NodeKind::Ident) return fail(node, "method:" + name);  // legacy: bare bindings only
    if (static_cast<KeyedMethod>(id) >= kFirstKeyedRead) return keyed_read_call(node, dst, id);
    const Binding* found = find_binding(text(receiver));
    if (!found) return fail(node, "method:" + name);
    // Bounded subset: const collections are refused (legacy's const check
    // order is not modelled). A loop binding is the iterated element itself
    // (legacy aliases it too), so its mutation reaches the container.
    if (found->const_array || found->const_scalar || found->readonly) return fail(node, "collection-const");
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
#if !(defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 13)
    if (!expr_to(ast_.at(node.a).a, state + Source)) return false;
#endif
    if (node.count > 0 && !expr_to(args[0], state + Item)) return false;
    if (node.count > 1 && !expr_to(args[1], state + Returned)) return false;
#if defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 13
    if (!expr_to(ast_.at(node.a).a, state + Source)) return false;  // M13: read after the arguments (H-A)
#endif
    fb_->set_offset(collection_offset(node));
    fb_->emit(OpCode::KeyedMutate, state, static_cast<std::uint16_t>(id), 0);
    fb_->emit(OpCode::Move, dst, state + Result);
    for (std::uint16_t k = 0; k < Count; ++k) fb_->emit(OpCode::LoadNull, state + k);
    fb_->release_to(mark);
    return builder_ok();
}

} // namespace nift::rt::detail
