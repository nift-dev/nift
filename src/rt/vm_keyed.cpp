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

bool Vm::refuse_reference_cycle(Fault& fault, const Proto* proto, const Instr& in, const HeapObject* receiver,
                                const Value& stored, const std::string& legacy_text) {
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 5
    return false;  // R5: no cycle check (the stored value may close a cycle and leak)
#endif
    if (!would_close_cycle(CycleTarget::aggregate(receiver), stored, cycle_graph_)) return false;
    const std::string text = !legacy_text.empty() && legacy_reference_cycle(receiver, stored)
                                 ? legacy_text
                                 : std::string("reference cycle: the stored value would contain itself");
    const auto at = static_cast<std::size_t>(&in - proto->code.data());
    raise_catchable(fault, "runtime.reference_cycle", text, proto, at < proto->offsets.size() ? proto->offsets[at] : 0);
    return true;
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
    const std::size_t n = collection_items(c).size();
    // Budget before growth (as array mutation): a reallocation of its storage.
    // A shared collection is mutated in place, so no clone is ever reserved.
    std::size_t grow = 0;
    if (m >= kFirstKeyedRead) {  // reads: nothing to reserve
    } else if (m == K::MapSet || m == K::SetAdd || m == K::StackPush) {
        const std::size_t step = m == K::MapSet ? 2 : 1;
        const std::size_t capacity = c.heap_as<CollectionObject>()->items.capacity();
        grow = n + step > capacity ? 2 * (n + step) * sizeof(Value) + 4 * (n + step) * sizeof(std::uint32_t) : 0;
    }
    if (grow) {
        natives::Context context;
        context.heap_limit = heap_limit_;
        // The VM's budget stop (as memory_exceeded), before any storage grows.
        if (!natives::reserve_result(context, grow))
            return raise(fault, StopReason::Internal, "memory budget exceeded");
    }
    // D-NEXT-refsem #4: a store that would close a reference cycle is refused
    // before anything is stored, after legacy's own argument checks (a
    // callable key or member may capture the receiver's cell too).
    const char* legacy = keyed_cycle_legacy_text(m);
    const auto cycle = [&](const Value& stored, const char* text) {
        return refuse_reference_cycle(fault, proto, in, c.heap(), stored, text ? text : "");
    };
    switch (m) {
        case K::MapSet:
            if (!natives::collection_key_ok(s[Item])) return fail("map: key must be bool, number, or string");
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 14
            map_set(c, s[Item], s[Returned]);  // R14: the store happens before the refusal (graph changed)
#endif
            if (cycle(s[Item], nullptr) || cycle(s[Returned], legacy)) return false;
            map_set(c, s[Item], s[Returned]);
            s[Result] = s[Returned];
            break;
        case K::MapRemove: {
            const std::ptrdiff_t at = collection_find(c, s[Item]);
            if (at < 0) return fail("remove: key not found");
            s[Result] = collection_items(c)[static_cast<std::size_t>(at) * 2 + 1];
            collection_erase(c, static_cast<std::size_t>(at));
            break;
        }
        case K::SetAdd:
            if (!natives::collection_key_ok(s[Item])) return fail("add: set values must be bool, number, or string");
            if (cycle(s[Item], nullptr)) return false;
            set_add(c, s[Item]);
            s[Result] = s[Item];
            break;
        case K::SetRemove: {
            if (!natives::collection_key_ok(s[Item]))
                return fail("remove: set values must be bool, number, or string");
            const std::ptrdiff_t at = collection_find(c, s[Item]);
            if (at >= 0) collection_erase(c, static_cast<std::size_t>(at));  // a missing member is a no-op
            s[Result] = s[Item];
            break;
        }
        case K::StackPush:
            if (cycle(s[Item], legacy)) return false;
            stack_push(c, s[Item]);
            s[Result] = s[Item];
            break;
        case K::StackPop:
            if (!n) return fail("pop: collection is empty");
            s[Result] = collection_items(c).back();
            stack_pop(c);
            break;
        case K::Clear:
            collection_clear(c);
            s[Result] = Value();
            break;
        case K::MapGet: {
            const std::ptrdiff_t at = collection_find(c, s[Item]);
            if (at < 0) return fail("get: key not found");
            s[Result] = collection_items(c)[static_cast<std::size_t>(at) * 2 + 1];
            return true;
        }
        case K::SetContains:
            if (!natives::collection_key_ok(s[Item]))
                return fail("contains: set values must be bool, number, or string");
            s[Result] = Value::boolean(collection_find(c, s[Item]) >= 0);
            return true;
        case K::StackTop: case K::StackFront:
            if (!n) return fail(m == K::StackTop ? "top: collection is empty" : "front: collection is empty");
            s[Result] = m == K::StackTop ? collection_items(c).back() : collection_items(c).front();
            return true;
        default: return internal(fault, "keyed mutation method");
    }
    return memory_ok(fault);
}
} // namespace nift::rt
