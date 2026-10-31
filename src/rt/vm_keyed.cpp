#include "rt/vm.h"
#include "rt/aggregate.h"
#include "rt/collection.h"
#include "rt/heap.h"
#include "rt/keyed.h"
#include "rt/natives/collection_methods.h"
#include "rt/natives/support.h"
// D-063: map/set/stack mutators on a state window (rt/keyed.h). Every store
// goes through the copy-on-write value layer (rt/aggregate.h), which clones a
// shared collection first, so the mutation is never observable through a copy.
namespace nift::rt {
bool Vm::op_keyed_mutate(const Instr& in, Value* R, Fault& fault, const Proto*) {
    using namespace collection_slot;
    using K = KeyedMethod;
    Value* s = R + in.a;
    if (in.b >= static_cast<std::uint16_t>(K::Count) || !s[Source].is(Kind::Collection))
        return internal(fault, "keyed mutation receiver");
    const auto m = static_cast<K>(in.b);
    const auto fail = [&](const char* text) { return raise(fault, StopReason::Error, text); };
    Value& c = s[Source];
    const std::size_t n = collection_items(c).size();
    // Budget before growth (as array mutation): a clone of a shared receiver,
    // and a reallocation of its storage.
    std::size_t grow = 0;
    if (m >= kFirstKeyedRead) {  // reads: nothing to reserve
    } else if (m == K::MapSet || m == K::SetAdd || m == K::StackPush) {
        const std::size_t step = m == K::MapSet ? 2 : 1;
        const std::size_t capacity = c.heap_as<CollectionObject>()->items.capacity();
        grow = n + step > capacity ? 2 * (n + step) * sizeof(Value) + 4 * (n + step) * sizeof(std::uint32_t) : 0;
    }
    const std::size_t clone = m < kFirstKeyedRead && c.refcount() > 1 ? n * sizeof(Value) + 128 : 0;
    if (grow + clone) {
        natives::Context context;
        context.heap_limit = heap_limit_;
        if (!natives::reserve_result(context, grow + clone)) return internal(fault, "memory budget exceeded");
    }
    switch (m) {
        case K::MapSet:
            if (!natives::collection_key_ok(s[Item])) return fail("map: key must be bool, number, or string");
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
