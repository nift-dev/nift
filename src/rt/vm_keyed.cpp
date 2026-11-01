#include "rt/vm_keyed.h"

#include "rt/vm.h"
#include "rt/aggregate.h"
#include "rt/collection.h"
#include "rt/heap.h"
#include "rt/keyed.h"
#include "rt/natives/collection_methods.h"
#include "rt/natives/support.h"
#include "rt/value_cycle.h"
// D-063: map/set/stack mutators on a state window (rt/keyed.h). Collections
// are references (D-NEXT-refsem #1): the mutation changes the shared object in
// place, visibly through every alias. A store that would close a reference
// cycle is refused before anything changes (D-NEXT-refsem #4).
namespace nift::rt {
const char* keyed_cycle_legacy_text(KeyedMethod method) noexcept {
    if (method == KeyedMethod::MapSet) return "set would create a cyclic reference";
    if (method == KeyedMethod::StackPush) return "push would create a cyclic reference";
    return nullptr;
}

mutation::Env Vm::mutation_env(const Note* note) const {
    mutation::Env env;
    env.graph = &cycle_graph_;
    env.heap_limit = heap_limit_;
    if (note != nullptr) env.receiver_text = note->text;
    return env;
}

bool Vm::mutation_failed(Fault& fault, const Proto* proto, const Instr& in, const mutation::Outcome& outcome,
                         const HeapObject* receiver, const Value& stored, const std::string& legacy_text) {
    using mutation::Failure;
    if (outcome.failure == Failure::Budget) return raise(fault, StopReason::Internal, "memory budget exceeded");
    if (outcome.failure != Failure::Cycle) return raise(fault, StopReason::Error, outcome.text);
    const std::string text = !legacy_text.empty() && legacy_reference_cycle(receiver, stored)
                                 ? legacy_text
                                 : std::string("reference cycle: the stored value would contain itself");
    const auto at = static_cast<std::size_t>(&in - proto->code.data());
    raise_catchable(fault, "runtime.reference_cycle", text, proto, at < proto->offsets.size() ? proto->offsets[at] : 0);
    return false;
}

bool Vm::op_keyed_mutate(const Instr& in, Value* R, Fault& fault, const Proto* proto) {
    using namespace collection_slot;
    using K = KeyedMethod;
    Value* s = R + in.a;
    if (in.b >= static_cast<std::uint16_t>(K::Count) || !s[Source].is(Kind::Collection))
        return internal(fault, "keyed mutation receiver");
    const auto m = static_cast<K>(in.b);
    const auto fail = [&](const char* text) { return raise(fault, StopReason::Error, text); };
    Value& c = s[Source];
    const std::size_t n = collection_item_count(c);
    // B1: every mutator goes through the mutation protocol (rt/mutation.h):
    // immutability, legacy's argument checks, the cycle check for each stored
    // value (D-NEXT-refsem #4), the budget before growth, then one commit.
    const mutation::Env env = mutation_env(proto->note(&in - proto->code.data()));
    const char* legacy = keyed_cycle_legacy_text(m);
    const auto failed = [&](const mutation::Outcome& out, const Value* stored, const char* text) {
        return mutation_failed(fault, proto, in, out, c.heap(), stored ? *stored : Value(), text ? text : "");
    };
    mutation::Outcome out;
    switch (m) {
        case K::MapSet:
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 14
            if (natives::collection_key_ok(s[Item]))
                map_set(c, s[Item], s[Returned]);  // rt-lint: storage-ok R14: the store before the refusal
#endif
            out = mutation::map_put(c, s[Item], s[Returned], env);
            if (!out.ok()) return failed(out, out.operand ? &s[Returned] : &s[Item], out.operand ? legacy : nullptr);
            s[Result] = s[Returned];
            break;
        case K::MapRemove: case K::SetRemove: {
            Value removed;
            out = mutation::collection_remove(c, s[Item], &removed, env);
            if (!out.ok()) return failed(out, nullptr, nullptr);
            s[Result] = m == K::MapRemove ? removed : s[Item];
            break;
        }
        case K::SetAdd:
            out = mutation::set_insert(c, s[Item], env);
            if (!out.ok()) return failed(out, &s[Item], nullptr);
            s[Result] = s[Item];
            break;
        case K::StackPush:
            out = mutation::stack_push(c, s[Item], env);
            if (!out.ok()) return failed(out, &s[Item], legacy);
            s[Result] = s[Item];
            break;
        case K::StackPop:
            out = mutation::stack_pop(c, &s[Result], env);
            if (!out.ok()) return failed(out, nullptr, nullptr);
            break;
        case K::Clear:
            out = mutation::clear(c, env);
            if (!out.ok()) return failed(out, nullptr, nullptr);
            s[Result] = Value();
            break;
        case K::MapGet: {
            const std::ptrdiff_t at = collection_find(c, s[Item]);
            if (at < 0) return fail("get: key not found");
            s[Result] = collection_view(c)[static_cast<std::size_t>(at) * 2 + 1];
            return true;
        }
        case K::SetContains:
            if (!natives::collection_key_ok(s[Item]))
                return fail("contains: set values must be bool, number, or string");
            s[Result] = Value::boolean(collection_find(c, s[Item]) >= 0);
            return true;
        case K::StackTop: case K::StackFront:
            if (!n) return fail(m == K::StackTop ? "top: collection is empty" : "front: collection is empty");
            s[Result] = m == K::StackTop ? collection_view(c).back() : collection_view(c).front();
            return true;
        default: return internal(fault, "keyed mutation method");
    }
    return memory_ok(fault);
}
} // namespace nift::rt
