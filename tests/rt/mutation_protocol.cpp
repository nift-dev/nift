// B1 mutation protocol (docs/runtime-2/checkpoints/b1-mutation.md), with B2's
// reference representation (b2-refrep.md): every mutator x every validation
// failure (kind, immutable, bounds, argument, cycle, budget) leaves the
// receiver byte-identical, the live heap equal and every refcount equal;
// immutability through C++ aliases (B2: no copy-on-write escape); copy and
// deepcopy clear the bit; the cycle check covers array and object receivers;
// the bookkeeping tallies the heap. Built with NIFT_MUTATION_MUTANT=1..5 it
// must fail.
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
    // B2: an array receiver is cycle-checked too (each stored value; the
    // splice replacement's elements), before anything changes.
    {
        Value r = a123();
        expect_refused("push-self", Failure::Cycle, r, {r}, push);
        expect_refused("push-nested", Failure::Cycle, r, {array_of({array_of({r})})}, push);
        expect_refused("insert", Failure::Cycle, r, {array_of({r})}, [&](Value& v, V& o) { return m::insert(v, 0, o[0], e); });
        expect_refused("set", Failure::Cycle, r, {closure_over(r)}, [&](Value& v, V& o) { return m::set(v, 0, o[0], e); });
        expect_refused("splice", Failure::Cycle, r, {array_of({array_of({r})})}, spl);
    }
    // Budget: growth beyond the capacity (B2: no copy-on-write clone to
    // reserve, so a shared receiver reserves nothing more), and splice's side
    // buffer.
    const m::Env b = env_budget();
    const Value shared = a123();
    for (const auto& [name, op] : std::vector<std::pair<const char*, std::function<m::Outcome(Value&, V&)>>>{
             {"insert", [&](Value& r, V& o) { return m::insert(r, 0, o[0], b); }},
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
        Value o = obj();
        expect_refused("object_put", Failure::Cycle, o, {o}, oput);
        expect_refused("object_put-nested", Failure::Cycle, o, {array_of({o})}, oput);
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
    // B2: an array is a reference, so a C++ alias is refused too (the B1
    // copy-on-write escape is gone), and a mutable alias shares the change.
    const Snap before = snap(args, {});
    Value alias = args;
    CHECK(m::push(alias, Value::integer(4), e).failure == Failure::Immutable && alias.heap() == args.heap());
    alias = Value();
    CHECK(snap(args, {}) == before && m::is_immutable(args));
    Value plain = a123();
    Value shared = plain;
    CHECK(m::push(shared, Value::integer(4), e).ok() && shared.heap() == plain.heap() && array_size(plain) == 4);
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
    // B2: copy() of any array or object is a new outer object (shared contents).
    Value inner = a123(), outer = array_of({inner}), copied;
    CHECK(shallow_copy(outer, copied, static_cast<std::size_t>(-1)) == CopyStatus::Ok);
    CHECK(copied.heap() != outer.heap() && array_view(copied)[0].heap() == inner.heap());
    // The cooperative-spawn copy keeps the bit (deep_copy_tuple keep_immutable).
    Value kept;
    CHECK(deep_copy_tuple(&args, 1, &kept, static_cast<std::size_t>(-1), true) == CopyStatus::Ok);
    CHECK(kept.heap() != args.heap() && m::is_immutable(kept));
}

void cycle_targets_and_bookkeeping() {
    // The cycle check covers array and object targets (B2: applies to them).
    const CycleGraph graph;
    Value a = a123(), o = obj();
    Value holder = inst();
    struct_set(holder, atom("x"), a);
    CHECK(m::closes_cycle(a.heap(), holder, &graph));
    CHECK(m::closes_cycle(o.heap(), array_of({o}), &graph));
    CHECK(!m::closes_cycle(a.heap(), array_of({o}), &graph));
    CHECK(m::cycle_applies(a) && m::cycle_applies(o) && m::cycle_applies(holder));
    // B2 summary (plan 5.1): a flat array keeps the bit clear, so storing it
    // walks nothing; a nested array marks its parent; a flat array that is
    // the target below a marked parent is still found.
    Value flat = a123(), nest = array_of({flat});
    CHECK(!may_contain_identity(flat) && may_contain_identity(nest));
    const auto walks = cycle_check_stats().checks;
    CHECK(!m::closes(a, flat, &graph) && cycle_check_stats().checks == walks);
    CHECK(m::closes(flat, array_of({nest}), &graph));
    // Bookkeeping: an edit that grows the storage is counted (M-B1c).
    Value grow = make_array(64);
    for (int k = 0; k < 64; ++k) array_push(grow, Value::integer(k));  // full: the insert doubles the storage
    const std::size_t live = live_heap_bytes(), counted = heap_footprint(grow.heap());
    CHECK(m::insert(grow, 0, Value::integer(1), m::Env{}).ok() && heap_footprint(grow.heap()) > counted);
    CHECK(live_heap_bytes() == live - counted + heap_footprint(grow.heap()) && array_size(grow) == 65);
    // B2: the receiver as its own replacement inserts its elements before the
    // removal (beginning, middle, end, full range, empty receiver, an alias),
    // and a replacement holding the receiver is a refused cycle.
    const auto spliced = [](std::size_t at, std::size_t count, std::size_t n) {
        Value self = make_array(n);
        for (std::size_t k = 0; k < n; ++k) array_push(self, Value::integer(static_cast<std::int64_t>(k + 1)));
        Value alias = self;
        const bool ok = m::splice(self, at, count, alias, nullptr, m::Env{}).ok();
        std::string out;
        for (const Value& v : array_view(self)) out += std::to_string(v.as_int());
        return ok && self.heap() == alias.heap() ? out : std::string("refused");
    };
    CHECK(spliced(0, 0, 2) == "1212" && spliced(1, 1, 3) == "11233" && spliced(2, 0, 2) == "1212");
    CHECK(spliced(0, 3, 3) == "123" && spliced(0, 0, 0) == "");
    Value inner = a123();
    // Operands that are references INTO the receiver's storage (each mutator
    // copies its operand before the storage can move): push, insert, set,
    // map_put, set_insert and stack_push of the receiver's own element.
    {
        Value r = make_array(1);
        array_push(r, make_string("an element long enough to live on the heap"));
        CHECK(m::push(r, array_view(r)[0], m::Env{}).ok() && m::insert(r, 0, array_view(r)[1], m::Env{}).ok());
        CHECK(m::set(r, 2, array_view(r)[0], m::Env{}).ok() && array_size(r) == 3);
        for (const Value& v : array_view(r)) CHECK(string_view_of(v) == "an element long enough to live on the heap");
        Value c = make_collection(CollectionKind::Map);
        map_set(c, make_string("key"), make_string("a value long enough to live on the heap"));
        CHECK(m::map_put(c, make_string("other"), collection_view(c)[1], m::Env{}).ok() && collection_size(c) == 2);
        Value st = make_collection(CollectionKind::Stack);
        stack_push(st, make_string("an element long enough to live on the heap"));
        for (int k = 0; k < 20; ++k) CHECK(m::stack_push(st, collection_view(st)[0], m::Env{}).ok());
        CHECK(collection_size(st) == 21);
    }
    Value nesting = array_of({inner});
    CHECK(m::splice(inner, 0, 0, nesting, nullptr, m::Env{}).failure == Failure::Cycle && array_size(inner) == 3);
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
