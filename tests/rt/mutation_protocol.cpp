// B1 mutation protocol (docs/runtime-2/checkpoints/b1-mutation.md): every
// mutator x every validation failure (kind, immutable, bounds, argument,
// cycle, budget) leaves the receiver byte-identical, the live heap equal and
// every refcount equal; immutability through C++ aliases; copy/deepcopy clear
// the bit; the cycle check accepts array and object targets; the bookkeeping
// tallies the heap. Built with NIFT_MUTATION_MUTANT=1/2/3 it must fail.
#include "rt/aggregate.h"
#include "rt/copy_graph.h"
#include "rt/heap_account.h"
#include "rt/mutation.h"
#include "rt/string.h"
#include "rt/value_cycle.h"
#include "rt_test.h"

#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace nift::rt;
namespace m = nift::rt::mutation;
using m::Failure;

namespace {

template <class T> void bytes_of(std::vector<unsigned char>& out, const std::vector<T>& v) {
    const auto* p = reinterpret_cast<const unsigned char*>(v.data());
    out.insert(out.end(), p, p + v.size() * sizeof(T));
    out.push_back(static_cast<unsigned char>(v.size()));
    out.push_back(static_cast<unsigned char>(v.capacity()));
}

struct Snap {
    std::vector<unsigned char> bytes;
    std::vector<std::uint32_t> refcounts;
    std::size_t live = 0;
    bool operator==(const Snap& o) const { return bytes == o.bytes && refcounts == o.refcounts && live == o.live; }
};

Snap snap(const Value& receiver, const std::vector<Value>& operands) {
    Snap s;
    const auto* p = reinterpret_cast<const unsigned char*>(&receiver);
    s.bytes.assign(p, p + sizeof(Value));
    if (receiver.is_heap()) {
        s.bytes.push_back(receiver.heap()->tag);
        s.refcounts.push_back(receiver.refcount());
        switch (receiver.kind()) {
            case Kind::Array: bytes_of(s.bytes, receiver.heap_as<ArrayObject>()->items); break;
            case Kind::Object:
                bytes_of(s.bytes, receiver.heap_as<ObjectObject>()->entries);
                bytes_of(s.bytes, receiver.heap_as<ObjectObject>()->index);
                break;
            case Kind::Struct: bytes_of(s.bytes, receiver.heap_as<StructObject>()->fields); break;
            case Kind::Collection:
                bytes_of(s.bytes, receiver.heap_as<CollectionObject>()->items);
                bytes_of(s.bytes, receiver.heap_as<CollectionObject>()->index);
                break;
            default: break;
        }
    }
    for (const Value& v : operands) s.refcounts.push_back(v.is_heap() ? v.refcount() : 0);
    s.live = live_heap_bytes();
    return s;
}

int cases = 0;
// One mutator x one failure: the outcome is `expected` and nothing changed.
void expect_refused(const char* name, Failure expected, Value receiver, std::vector<Value> operands,
                    const std::function<m::Outcome(Value&, std::vector<Value>&)>& run) {
    ++cases;
    const Snap before = snap(receiver, operands);
    const m::Outcome out = run(receiver, operands);
    const Snap after = snap(receiver, operands);
    const bool ok = out.failure == expected && before == after;
    CHECK(ok);
    if (!ok) std::fprintf(stderr, "  case %s: failure %d (want %d), unchanged %d\n", name, int(out.failure),
                          int(expected), int(before == after));
}

Value array_of(std::initializer_list<Value> items) {
    Value a = make_array(items.size());
    for (const Value& v : items) array_push(a, v);
    return a;
}
Value immutable(Value v) { m::make_immutable(v); return v; }
Value a123() { return array_of({Value::integer(1), Value::integer(2), Value::integer(3)}); }
Value obj() { Value o = make_object(); object_set(o, atom("k"), Value::integer(1)); return o; }
Value inst() { Value s = make_struct(atom("P")); struct_add_field(s, atom("x"), Value::integer(1)); return s; }
Value coll(CollectionKind k, bool filled) {
    Value c = make_collection(k);
    if (filled && k == CollectionKind::Map) map_set(c, make_string("a"), Value::integer(1));
    if (filled && k == CollectionKind::Set) set_add(c, Value::integer(1));
    if (filled && k == CollectionKind::Stack) stack_push(c, Value::integer(1));
    return c;
}
Value closure_over(const Value& target) {  // a callable capturing `target` (the default graph's upvalues)
    auto* f = new FunctionObject();
    f->upvalues.push_back(target);
    return Value::adopt(f);
}

m::Env env_text() { m::Env e; e.receiver_text = "args"; return e; }
m::Env env_budget() { m::Env e; e.heap_limit = 0; return e; }

void array_matrix() {
    using V = std::vector<Value>;
    const m::Env e = env_text();
    const auto push = [&](Value& r, V& o) { return m::push(r, o[0], e); };
    const auto set = [&](Value& r, V& o) { return m::set(r, 5, o[0], e); };
    const auto ins = [&](Value& r, V& o) { return m::insert(r, 9, o[0], e); };
    const auto rem = [&](Value& r, V&) { return m::remove(r, 9, nullptr, e); };
    const auto pop = [&](Value& r, V&) { return m::pop(r, nullptr, e); };
    const auto rev = [&](Value& r, V&) { return m::reverse(r, e); };
    const auto clr = [&](Value& r, V&) { return m::clear(r, e); };
    const auto spl = [&](Value& r, V& o) { return m::splice(r, 0, 1, o[0], nullptr, e); };
    const Value x = Value::integer(7), arr = a123();
    for (const auto& [name, op] : std::vector<std::pair<const char*, std::function<m::Outcome(Value&, V&)>>>{
             {"push", push}, {"set", set}, {"insert", ins}, {"remove", rem}, {"pop", pop}, {"reverse", rev},
             {"clear", clr}, {"splice", spl}}) {
        expect_refused(name, Failure::Kind, obj(), {arr}, op);
        expect_refused(name, Failure::Immutable, immutable(a123()), {arr}, op);
    }
    // Immutable is checked before bounds (stage order 2.2 before 2.3).
    expect_refused("set-order", Failure::Immutable, immutable(a123()), {x}, set);
    expect_refused("set", Failure::Bounds, a123(), {x}, set);
    expect_refused("insert", Failure::Bounds, a123(), {x}, ins);
    expect_refused("remove", Failure::Bounds, a123(), {}, rem);
    expect_refused("pop", Failure::Bounds, make_array(), {}, pop);
    expect_refused("splice", Failure::Argument, a123(), {x}, spl);
    // Budget: a shared receiver reserves its clone; a full one its growth.
    const m::Env b = env_budget();
    const Value shared = a123();
    for (const auto& [name, op] : std::vector<std::pair<const char*, std::function<m::Outcome(Value&, V&)>>>{
             {"insert", [&](Value& r, V& o) { return m::insert(r, 0, o[0], b); }},
             {"remove", [&](Value& r, V&) { return m::remove(r, 0, nullptr, b); }},
             {"pop", [&](Value& r, V&) { return m::pop(r, nullptr, b); }},
             {"reverse", [&](Value& r, V&) { return m::reverse(r, b); }},
             {"clear", [&](Value& r, V&) { return m::clear(r, b); }},
             {"splice", [&](Value& r, V& o) { return m::splice(r, 0, 1, o[0], nullptr, b); }}})
        expect_refused(name, Failure::Budget, shared, {arr, shared}, op);
}

void keyed_matrix() {
    using V = std::vector<Value>;
    const m::Env e = env_text();
    const Value x = Value::integer(7), bad = a123();
    const auto oput = [&](Value& r, V& o) { return m::object_put(r, atom("k"), o[0], e); };
    const auto oerase = [&](Value& r, V&) { return m::object_erase(r, atom("missing"), e); };
    const auto fset = [&](Value& r, V& o) { return m::field_set(r, atom("x"), o[0], e); };
    const auto fmiss = [&](Value& r, V& o) { return m::field_set(r, atom("y"), o[0], e); };
    expect_refused("object_put", Failure::Kind, a123(), {x}, oput);
    expect_refused("object_put", Failure::Immutable, immutable(obj()), {x}, oput);
    expect_refused("object_erase", Failure::Kind, a123(), {}, oerase);
    expect_refused("object_erase", Failure::Immutable, immutable(obj()), {}, oerase);
    expect_refused("object_erase", Failure::Bounds, obj(), {}, oerase);
    expect_refused("field_set", Failure::Kind, obj(), {x}, fset);
    expect_refused("field_set", Failure::Immutable, immutable(inst()), {x}, fset);
    expect_refused("field_set", Failure::Bounds, inst(), {x}, fmiss);
    {
        Value s = inst();
        expect_refused("field_set", Failure::Cycle, s, {array_of({s})}, fset);
    }
    const auto mput = [&](Value& r, V& o) { return m::map_put(r, o[0], o[1], e); };
    const auto sadd = [&](Value& r, V& o) { return m::set_insert(r, o[0], e); };
    const auto crem = [&](Value& r, V& o) { return m::collection_remove(r, o[0], nullptr, e); };
    const auto spush = [&](Value& r, V& o) { return m::stack_push(r, o[0], e); };
    const auto spop = [&](Value& r, V&) { return m::stack_pop(r, nullptr, e); };
    const auto cclr = [&](Value& r, V&) { return m::clear(r, e); };
    const CollectionKind map = CollectionKind::Map, set = CollectionKind::Set, stack = CollectionKind::Stack;
    expect_refused("map_put", Failure::Kind, coll(set, true), {x, x}, mput);
    expect_refused("map_put", Failure::Immutable, immutable(coll(map, true)), {x, x}, mput);
    expect_refused("map_put", Failure::Argument, coll(map, true), {bad, x}, mput);
    {
        Value c = coll(map, true);
        expect_refused("map_put-key", Failure::Cycle, c, {closure_over(c), x}, mput);
        expect_refused("map_put-value", Failure::Cycle, c, {x, array_of({c})}, mput);
    }
    expect_refused("map_put", Failure::Budget, coll(map, false), {x, x},
                   [&](Value& r, V& o) { return m::map_put(r, o[0], o[1], env_budget()); });
    expect_refused("set_insert", Failure::Kind, coll(map, true), {x}, sadd);
    expect_refused("set_insert", Failure::Immutable, immutable(coll(set, true)), {x}, sadd);
    expect_refused("set_insert", Failure::Argument, coll(set, true), {bad}, sadd);
    {
        Value c = coll(set, true);
        expect_refused("set_insert", Failure::Cycle, c, {closure_over(c)}, sadd);
    }
    expect_refused("set_insert", Failure::Budget, coll(set, false), {x},
                   [&](Value& r, V& o) { return m::set_insert(r, o[0], env_budget()); });
    expect_refused("collection_remove", Failure::Kind, coll(stack, true), {x}, crem);
    expect_refused("collection_remove", Failure::Immutable, immutable(coll(map, true)), {make_string("a")}, crem);
    expect_refused("collection_remove", Failure::Bounds, coll(map, true), {x}, crem);
    expect_refused("collection_remove", Failure::Argument, coll(set, true), {bad}, crem);
    expect_refused("stack_push", Failure::Kind, coll(set, true), {x}, spush);
    expect_refused("stack_push", Failure::Immutable, immutable(coll(stack, true)), {x}, spush);
    {
        Value c = coll(stack, true);
        expect_refused("stack_push", Failure::Cycle, c, {array_of({c})}, spush);
    }
    expect_refused("stack_push", Failure::Budget, coll(stack, false), {x},
                   [&](Value& r, V& o) { return m::stack_push(r, o[0], env_budget()); });
    expect_refused("stack_pop", Failure::Kind, coll(map, true), {}, spop);
    expect_refused("stack_pop", Failure::Immutable, immutable(coll(stack, true)), {}, spop);
    expect_refused("stack_pop", Failure::Bounds, coll(stack, false), {}, spop);
    expect_refused("clear", Failure::Immutable, immutable(coll(set, true)), {}, cclr);
}

void aliases_and_copies() {
    const m::Env e = env_text();
    Value args = immutable(a123());
    const m::Outcome refused = m::push(args, Value::integer(4), e);
    CHECK(refused.failure == Failure::Immutable && refused.text == "cannot mutate const array: args");
    // A C++ alias of a copy-on-write array: the commit clones, so the
    // immutable object itself is never written (B1); the clone is mutable.
    const Snap before = snap(args, {});
    Value alias = args;
    CHECK(m::push(alias, Value::integer(4), e).ok());
    CHECK(alias.heap() != args.heap() && !m::is_immutable(alias) && array_size(alias) == 4);
    alias = Value();
    CHECK(snap(args, {}) == before && m::is_immutable(args));
    // References (struct, collection): immutable through every alias.
    Value s = immutable(inst());
    Value s2 = s;
    CHECK(m::field_set(s2, atom("x"), Value::integer(2), e).failure == Failure::Immutable);
    Value c = immutable(coll(CollectionKind::Map, true));
    Value c2 = c;
    CHECK(m::map_put(c2, make_string("b"), Value::integer(2), e).failure == Failure::Immutable);
    // copy() and deepcopy() of an immutable value are mutable.
    Value shallow, deep;
    CHECK(shallow_copy(args, shallow, static_cast<std::size_t>(-1)) == CopyStatus::Ok);
    CHECK(deep_copy(args, deep, static_cast<std::size_t>(-1)) == CopyStatus::Ok);
    CHECK(!m::is_immutable(shallow) && shallow.heap() != args.heap() && array_size(shallow) == 3);
    CHECK(!m::is_immutable(deep) && m::push(shallow, Value::integer(4), e).ok() && m::push(deep, Value(), e).ok());
    CHECK(array_size(args) == 3);
}

void cycle_targets_and_bookkeeping() {
    // The cycle check accepts array and object targets (reachable from B2).
    const CycleGraph graph;
    Value a = a123(), o = obj();
    Value holder = inst();
    struct_set(holder, atom("x"), a);
    CHECK(m::closes_cycle(a.heap(), holder, graph));
    CHECK(m::closes_cycle(o.heap(), array_of({o}), graph));
    CHECK(!m::closes_cycle(a.heap(), array_of({o}), graph));
    CHECK(!m::cycle_applies(a) && !m::cycle_applies(o) && m::cycle_applies(holder));
    // Bookkeeping: an edit that grows the storage is counted (M-B1c).
    Value grow = make_array(64);
    for (int k = 0; k < 64; ++k) array_push(grow, Value::integer(k));  // full: the insert doubles the storage
    const std::size_t live = live_heap_bytes(), counted = heap_footprint(grow.heap());
    CHECK(m::insert(grow, 0, Value::integer(1), m::Env{}).ok() && heap_footprint(grow.heap()) > counted);
    CHECK(live_heap_bytes() == live - counted + heap_footprint(grow.heap()) && array_size(grow) == 65);
    // A successful splice commits once, in place (identity kept).
    Value sp = a123();
    const HeapObject* id = sp.heap();
    Value removed;
    CHECK(m::splice(sp, 1, 1, array_of({Value::integer(8), Value::integer(9)}), &removed, m::Env{}).ok());
    CHECK(sp.heap() == id && array_size(sp) == 4 && array_size(removed) == 1);
}

} // namespace

int main() {
    for (const char* name : {"k", "missing", "x", "y", "P"}) atom(name);  // interned atoms count on the heap
    array_matrix();
    keyed_matrix();
    aliases_and_copies();
    cycle_targets_and_bookkeeping();
    std::printf("mutation protocol: %d refusal cases\n", cases);
    return rt_test::finish("mutation-protocol");
}
