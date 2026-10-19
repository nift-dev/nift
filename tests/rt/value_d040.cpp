// D-040 value-layer tests (L7 step 2.1 and 2.7a; semantics.md V1-V19):
//
//   value_d040 [seed] [scale]
//
// 1. structs are copy-on-write values: copies, nested place writes,
//    structural equality and hash (type and every field), struct vs object
// 2. nested copy-on-write against a deep-copy model: random copies, deep
//    place writes through array/object/struct slot chains, pushes and
//    self-stores over several slots; after every step each slot equals its
//    model and no value contains itself (acyclic, V16)
// 2b. self-insertion: arrays, objects and structs, two levels deep and
//    repeated; the stored value is the pre-write snapshot (no cycle)
// 3. same(): identity only for callables and handles, NotIdentity otherwise
// 4. the identity summary bit: sound (an aggregate with a callable, a handle
//    or a marked child below it is marked), precise for API stores of plain
//    data, conservative for raw array_mut
// 5. the cycle checker against brute-force reachability on random graphs of
//    captured cells, closures, handles and payloads; plain stores never walk
#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/heap.h"
#include "rt/string.h"
#include "rt/value_cycle.h"
#include "rt_test.h"

#include <cstdlib>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace nift::rt;

namespace {

std::mt19937_64 rng;
std::uint64_t below(std::uint64_t n) { return std::uniform_int_distribution<std::uint64_t>(0, n - 1)(rng); }
Value I(std::int64_t i) { return Value::integer(i); }

// ---- 1. structs -------------------------------------------------------------
void structs() {
    Value q = make_struct(atom("Q"));
    struct_add_field(q, atom("v"), I(1));
    Value p = make_struct(atom("P"));
    struct_add_field(p, atom("x"), I(1));
    struct_add_field(p, atom("s"), I(2));  // a private field takes part like any other
    struct_add_field(p, atom("q"), q);
    Value copy = p;
    CHECK(copy.heap() == p.heap());  // shared until written
    Value* inner = struct_slot_mut(copy, atom("q"));
    CHECK(inner && struct_set(*inner, atom("v"), I(7)));
    CHECK(struct_get(*struct_get(p, atom("q")), atom("v"))->as_int() == 1);
    CHECK(struct_get(*struct_get(copy, atom("q")), atom("v"))->as_int() == 7);
    CHECK(!equal(p, copy));
    CHECK(struct_set(*struct_slot_mut(copy, atom("q")), atom("v"), I(1)) && equal(p, copy) && hash(p) == hash(copy));
    CHECK(!struct_set(copy, atom("nope"), I(1)) && !struct_slot_mut(copy, atom("nope")));
    Value r = make_struct(atom("R"));  // same fields, other type
    struct_add_field(r, atom("x"), I(1));
    struct_add_field(r, atom("s"), I(2));
    struct_add_field(r, atom("q"), q);
    CHECK(!equal(p, r));
    Value o = make_object();
    object_set(o, atom("x"), I(1));
    Value px = make_struct(atom("P"));
    struct_add_field(px, atom("x"), I(1));
    CHECK(!equal(px, o) && !equal(o, px));
    Value priv = p;
    CHECK(struct_set(priv, atom("s"), I(3)) && !equal(p, priv));  // private fields count
    // A struct stored into its own field stores its previous value (V16).
    Value self = make_struct(atom("N"));
    struct_add_field(self, atom("next"), Value());
    CHECK(struct_set(self, atom("next"), self));
    const Value* next = struct_get(self, atom("next"));
    CHECK(next->is(Kind::Struct) && next->heap() != self.heap() && struct_get(*next, atom("next"))->is_null());
}

// ---- 2. nested copy-on-write vs a deep-copy model ---------------------------
struct Model {
    enum class K { Int, Arr, Obj, Str } k = K::Int;
    std::int64_t i = 0;
    std::string type;
    std::vector<Model> items;                       // Arr
    std::vector<std::pair<std::string, Model>> kv;  // Obj, Str (fields)
};

Model mk(Model::K k) { Model m; m.k = k; return m; }
Model mint(std::int64_t i) { Model m; m.i = i; return m; }

bool matches(const Value& v, const Model& m) {
    switch (m.k) {
        case Model::K::Int: return v.is(Kind::Int) && v.as_int() == m.i;
        case Model::K::Arr: {
            if (!v.is(Kind::Array) || array_size(v) != m.items.size()) return false;
            for (std::size_t n = 0; n < m.items.size(); ++n)
                if (!matches(array_items(v)[n], m.items[n])) return false;
            return true;
        }
        case Model::K::Obj: {
            if (!v.is(Kind::Object) || object_size(v) != m.kv.size()) return false;
            for (std::size_t n = 0; n < m.kv.size(); ++n)
                if (atom_text(object_entries(v)[n].first) != m.kv[n].first ||
                    !matches(object_entries(v)[n].second, m.kv[n].second))
                    return false;
            return true;
        }
        case Model::K::Str: {
            if (!v.is(Kind::Struct) || atom_text(struct_type(v)) != m.type || struct_fields(v).size() != m.kv.size())
                return false;
            for (std::size_t n = 0; n < m.kv.size(); ++n)
                if (atom_text(struct_fields(v)[n].first) != m.kv[n].first ||
                    !matches(struct_fields(v)[n].second, m.kv[n].second))
                    return false;
            return true;
        }
    }
    return false;
}

// The document of the owner's case: an object holding arrays of objects and
// a struct, so every write path crosses each aggregate kind.
Value base_doc(Model& m) {
    Value items = make_array();
    m = mk(Model::K::Obj);
    Model mi = mk(Model::K::Arr);
    for (int n = 0; n < 3; ++n) {
        Value item = make_object();
        object_set(item, atom("id"), I(n));
        Value tags = make_array();
        array_push(tags, I(10 * n));
        object_set(item, atom("tags"), tags);
        array_push(items, item);
        Model mo = mk(Model::K::Obj);
        mo.kv.push_back({"id", mint(n)});
        Model mt = mk(Model::K::Arr);
        mt.items.push_back(mint(10 * n));
        mo.kv.push_back({"tags", mt});
        mi.items.push_back(mo);
    }
    Value meta = make_struct(atom("Meta"));
    struct_add_field(meta, atom("n"), I(3));
    Model mm = mk(Model::K::Str);
    mm.type = "Meta";
    mm.kv.push_back({"n", mint(3)});
    Value doc = make_object();
    object_set(doc, atom("items"), items);
    object_set(doc, atom("meta"), meta);
    m.kv.push_back({"items", mi});
    m.kv.push_back({"meta", mm});
    return doc;
}

// Walks every heap object below `v` and fails if one is its own ancestor.
bool acyclic(const Value& v, std::vector<const HeapObject*>& path) {
    if (!(v.is(Kind::Array) || v.is(Kind::Object) || v.is(Kind::Struct))) return true;
    for (const HeapObject* p : path)
        if (p == v.heap()) return false;
    path.push_back(v.heap());
    bool ok = true;
    if (v.is(Kind::Array)) for (const Value& c : array_items(v)) ok = ok && acyclic(c, path);
    if (v.is(Kind::Object)) for (const auto& e : object_entries(v)) ok = ok && acyclic(e.second, path);
    if (v.is(Kind::Struct)) for (const auto& f : struct_fields(v)) ok = ok && acyclic(f.second, path);
    path.pop_back();
    return ok;
}

void nested_cow(long sequences) {
    constexpr int kSlots = 4;
    for (long s = 0; s < sequences; ++s) {
        std::vector<Value> slot(kSlots);
        std::vector<Model> model(kSlots);
        slot[0] = base_doc(model[0]);
        for (int k = 1; k < kSlots; ++k) { slot[k] = slot[0]; model[k] = model[0]; }
        for (int step = 0; step < 40; ++step) {
            const int a = static_cast<int>(below(kSlots)), b = static_cast<int>(below(kSlots));
            const std::int64_t x = static_cast<std::int64_t>(below(1000));
            const std::size_t item = below(3);
            switch (below(7)) {
                case 0: slot[a] = slot[b]; model[a] = model[b]; break;  // copy
                case 1: {  // deep write doc["items"][item]["tags"][0] = x
                    Value* items = object_slot_mut(slot[a], atom("items"));
                    Value* it = items ? array_slot_mut(*items, item % array_size(*items)) : nullptr;
                    Value* tags = it ? object_slot_mut(*it, atom("tags")) : nullptr;
                    if (tags && array_size(*tags) > 0) {
                        array_set(*tags, 0, I(x));
                        auto& mi = model[a].kv[0].second.items;
                        mi[item % mi.size()].kv[1].second.items[0] = mint(x);
                    }
                    break;
                }
                case 2: {  // push into a nested array: doc["items"][item]["tags"].push(x)
                    Value* items = object_slot_mut(slot[a], atom("items"));
                    Value* it = array_slot_mut(*items, item % array_size(*items));
                    array_push(*object_slot_mut(*it, atom("tags")), I(x));
                    auto& mi = model[a].kv[0].second.items;
                    mi[item % mi.size()].kv[1].second.items.push_back(mint(x));
                    break;
                }
                case 3: {  // struct field through a path: doc.meta.n = x
                    CHECK(struct_set(*object_slot_mut(slot[a], atom("meta")), atom("n"), I(x)));
                    model[a].kv[1].second.kv[0].second = mint(x);
                    break;
                }
                case 4: {  // self-store: doc["items"].push(doc["items"]) stores the previous value
                    Value* items = object_slot_mut(slot[a], atom("items"));
                    const Value previous = *items;
                    array_push(*items, previous);
                    auto& mi = model[a].kv[0].second;
                    const Model before = mi;
                    if (mi.items.size() < 12) { mi.items.push_back(before); } else { array_mut(*items).pop_back(); }
                    break;
                }
                case 5: {  // whole-document self-store under a new key
                    if (object_size(slot[a]) < 4) {
                        const Value previous = slot[a];
                        object_set(slot[a], atom("old" + std::to_string(object_size(slot[a]))), previous);
                        const Model before = model[a];
                        model[a].kv.push_back({"old" + std::to_string(model[a].kv.size()), before});
                    }
                    break;
                }
                default: {  // repeated writes to one copy
                    Value* items = object_slot_mut(slot[a], atom("items"));
                    for (int rep = 0; rep < 3; ++rep) {
                        Value* it = array_slot_mut(*items, item % array_size(*items));
                        object_set(*it, atom("id"), I(x + rep));
                    }
                    auto& mi = model[a].kv[0].second.items;
                    mi[item % mi.size()].kv[0].second = mint(x + 2);
                    break;
                }
            }
            for (int k = 0; k < kSlots; ++k) {
                CHECK(matches(slot[k], model[k]));
                std::vector<const HeapObject*> path;
                CHECK(acyclic(slot[k], path));
            }
        }
    }
}


// ---- 2b. self-insertion (owner requirement): the copy-on-write detach happens
// before the insert, so the stored value is the pre-write snapshot; no graph
// cycle is ever built and later writes to either side stay independent.
std::string show(const Value& v) {
    switch (v.kind()) {
        case Kind::Int: return std::to_string(v.as_int());
        case Kind::Array: {
            std::string out = "[";
            for (std::size_t n = 0; n < array_size(v); ++n) out += (n ? "," : "") + show(array_items(v)[n]);
            return out + "]";
        }
        case Kind::Object: {
            std::string out = "{";
            for (std::size_t n = 0; n < object_size(v); ++n)
                out += (n ? "," : "") + std::string(atom_text(object_entries(v)[n].first)) + ":" +
                       show(object_entries(v)[n].second);
            return out + "}";
        }
        case Kind::Struct: {
            std::string out = std::string(atom_text(struct_type(v))) + "{";
            for (std::size_t n = 0; n < struct_fields(v).size(); ++n)
                out += (n ? "," : "") + std::string(atom_text(struct_fields(v)[n].first)) + ":" +
                       show(struct_fields(v)[n].second);
            return out + "}";
        }
        default: return "?";
    }
}

bool acyclic_value(const Value& v) {
    std::vector<const HeapObject*> path;
    return acyclic(v, path);
}

void self_insertion() {
    // a = [1]; a.push(a) -> [1,[1]]; later writes to a or a[1] stay independent.
    Value a = make_array();
    array_push(a, I(1));
    array_push(a, a);
    CHECK(show(a) == "[1,[1]]" && acyclic_value(a));
    array_push(a, I(2));
    CHECK(show(a) == "[1,[1],2]");
    array_push(*array_slot_mut(a, 1), I(9));
    CHECK(show(a) == "[1,[1,9],2]");
    // Repeated self-insertion: each step stores the previous snapshot.
    Value r = make_array();
    array_push(r, I(0));
    std::string expect = "[0]";
    for (int n = 0; n < 5; ++n) {
        array_push(r, r);
        expect = expect.substr(0, expect.size() - 1) + "," + expect + "]";
        CHECK(show(r) == expect && acyclic_value(r));
    }
    // Two levels deep, array: a[0][0].push(a) stores the whole previous a.
    Value d = make_array();
    { Value inner = make_array(); Value leaf = make_array(); array_push(leaf, I(1)); array_push(inner, leaf);
      array_push(d, inner); }
    const Value snap = d;
    array_push(*array_slot_mut(*array_slot_mut(d, 0), 0), snap);
    CHECK(show(d) == "[[[1,[[[1]]]]]]" && show(snap) == "[[[1]]]" && acyclic_value(d));
    // Objects, two levels: o["k"]["m"].push(o) and o["self"] = o.
    Value o = make_object();
    { Value k = make_object(); Value m = make_array(); array_push(m, I(1)); object_set(k, atom("m"), m);
      object_set(o, atom("k"), k); }
    const Value before = o;
    array_push(*object_slot_mut(*object_slot_mut(o, atom("k")), atom("m")), before);
    CHECK(show(o) == "{k:{m:[1,{k:{m:[1]}}]}}" && acyclic_value(o));
    object_set(o, atom("self"), o);
    CHECK(show(o) == "{k:{m:[1,{k:{m:[1]}}]},self:{k:{m:[1,{k:{m:[1]}}]}}}" && acyclic_value(o));
    array_push(*object_slot_mut(*object_slot_mut(o, atom("k")), atom("m")), I(5));
    CHECK(show(*object_get(o, atom("self"))) == "{k:{m:[1,{k:{m:[1]}}]}}");
    // Structs, two levels: s.inner.list.push(s) and s.next = s.
    Value s = make_struct(atom("S"));
    { Value inner = make_struct(atom("In")); struct_add_field(inner, atom("list"), make_array());
      struct_add_field(s, atom("inner"), inner); struct_add_field(s, atom("next"), Value()); }
    const Value s0 = s;
    array_push(*struct_slot_mut(*struct_slot_mut(s, atom("inner")), atom("list")), s0);
    CHECK(struct_set(s, atom("next"), s));
    CHECK(show(s) == "S{inner:In{list:[S{inner:In{list:[]},next:?}]},next:S{inner:In{list:[S{inner:In{list:[]},next:?}]},next:?}}");
    CHECK(acyclic_value(s));
    CHECK(struct_set(*struct_slot_mut(s, atom("next")), atom("next"), I(3)));
    CHECK(show(*struct_get(s, atom("next"))) == "S{inner:In{list:[S{inner:In{list:[]},next:?}]},next:3}");
    CHECK(struct_get(s, atom("next"))->heap() != s.heap());
    // Self-insertion through a copy-on-write share that another slot still holds.
    Value x = make_array();
    array_push(x, I(1));
    Value y = x;
    array_push(x, y);
    CHECK(show(x) == "[1,[1]]" && show(y) == "[1]" && acyclic_value(x));
    // Ordinary self-stores never reach the cycle checker.
    CycleCheckStats before_stats = cycle_check_stats();
    array_push(a, a);
    object_set(o, atom("again"), o);
    CHECK(cycle_check_stats().checks == before_stats.checks);
}

// ---- 3. same() ----------------------------------------------------------------
void sameness() {
    Value f = Value::adopt(new FunctionObject());
    Value g = Value::adopt(new FunctionObject());
    Value f2 = f;
    Value h = Value::handle(HandleKind::File, 7, 1);
    Value h2 = Value::handle(HandleKind::File, 7, 1);
    Value h3 = Value::handle(HandleKind::File, 7, 2);
    CHECK(same(f, f2) == Sameness::Same && same(f, g) == Sameness::Different);
    CHECK(same(h, h2) == Sameness::Same && same(h2, h) == Sameness::Same && same(h, h3) == Sameness::Different);
    CHECK(same(f, h) == Sameness::Different);
    Value a = make_array();
    Value b = a;  // storage sharing is never exposed
    Value st = make_struct(atom("P"));
    Value m = make_object();
    for (const Value* ordinary : {&a, &b, &st, &m}) {
        CHECK(same(*ordinary, *ordinary) == Sameness::NotIdentity);
        CHECK(same(*ordinary, f) == Sameness::NotIdentity && same(f, *ordinary) == Sameness::NotIdentity);
    }
    CHECK(same(I(1), I(1)) == Sameness::NotIdentity && same(make_string("x"), h) == Sameness::NotIdentity);
    CHECK(identity_bearing(f) && identity_bearing(h) && !identity_bearing(a) && !identity_bearing(st));
}

// ---- 4. the identity summary bit -----------------------------------------------
bool contains_identity(const Value& v) {
    if (identity_bearing(v)) return true;
    if (v.is(Kind::Array)) for (const Value& c : array_items(v)) if (contains_identity(c)) return true;
    if (v.is(Kind::Object)) for (const auto& e : object_entries(v)) if (contains_identity(e.second)) return true;
    if (v.is(Kind::Struct)) for (const auto& f : struct_fields(v)) if (contains_identity(f.second)) return true;
    return false;
}

void summary_bit(long rounds) {
    for (long r = 0; r < rounds; ++r) {
        std::vector<Value> pool = {make_array(), make_object(), make_struct(atom("S"))};
        for (int step = 0; step < 30; ++step) {
            Value item;
            switch (below(6)) {
                case 0: item = Value::adopt(new FunctionObject()); break;
                case 1: item = Value::handle(HandleKind::Timer, below(5) + 1, 1); break;
                case 2: item = pool[below(pool.size())]; break;
                default: item = I(static_cast<std::int64_t>(below(100))); break;
            }
            Value& target = pool[below(pool.size())];
            if (target.is(Kind::Array)) array_push(target, item);
            else if (target.is(Kind::Object)) object_set(target, atom("k" + std::to_string(below(4))), item);
            else struct_add_field(target, atom("f" + std::to_string(step)), item);
            if (below(4) == 0) pool.push_back(target);  // a shared copy
        }
        for (const Value& v : pool) CHECK(!contains_identity(v) || may_contain_identity(v));  // sound
    }
    // Precise for API stores of plain data; conservative for raw storage.
    Value plain = make_array();
    for (int n = 0; n < 50; ++n) array_push(plain, I(n));
    Value obj = make_object();
    object_set(obj, atom("a"), plain);
    Value st = make_struct(atom("S"));
    struct_add_field(st, atom("o"), obj);
    array_set(plain, 0, I(5));
    CHECK(!may_contain_identity(plain) && !may_contain_identity(obj) && !may_contain_identity(st));
    array_mut(plain)[1] = I(6);
    CHECK(may_contain_identity(plain));  // raw access: marked conservatively
    Value copy = plain;
    array_push(copy, I(1));
    CHECK(may_contain_identity(copy));  // clones keep the bit
    Value holder = make_array();
    array_push(holder, Value::adopt(new FunctionObject()));
    Value outer = make_object();
    object_set(outer, atom("h"), holder);
    CHECK(may_contain_identity(holder) && may_contain_identity(outer));
}

// ---- 5. the cycle checker -----------------------------------------------------
// A test graph: `cells` are captured variables (identity = their address),
// each closure captures a set of cells, each handle id has a payload.
class TestGraph : public CycleGraph {
public:
    std::vector<Value> cells;
    std::map<const FunctionObject*, std::vector<std::size_t>> captures;
    std::map<std::uint64_t, Value> payloads;
    void captured(const FunctionObject& f, CaptureVisitor& visitor) const override {
        auto it = captures.find(&f);
        if (it == captures.end()) return;
        for (std::size_t c : it->second) visitor.cell(&cells[c], cells[c]);
    }
    const Value* payload(const Value& h) const override {
        auto it = payloads.find(h.handle_id());
        return it == payloads.end() ? nullptr : &it->second;
    }
};

// Brute force: every value reachable from `v`, ignoring summary bits.
bool reaches(const TestGraph& g, const Value& v, const CycleTarget& t, std::set<const void*>& seen) {
    switch (v.kind()) {
        case Kind::Function: {
            const auto* f = v.heap_as<FunctionObject>();
            if (!seen.insert(f).second) return false;
            auto it = g.captures.find(f);
            if (it == g.captures.end()) return false;
            for (std::size_t c : it->second) {
                if (t.kind == CycleTarget::Kind::Cell && t.cell == &g.cells[c]) return true;
                if (seen.insert(&g.cells[c]).second && reaches(g, g.cells[c], t, seen)) return true;
            }
            return false;
        }
        case Kind::Handle: {
            if (t.kind == CycleTarget::Kind::Resource && t.handle.handle_id() == v.handle_id()) return true;
            const void* key = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(v.handle_id() * 8 + 1));
            if (!seen.insert(key).second) return false;
            auto it = g.payloads.find(v.handle_id());
            return it != g.payloads.end() && reaches(g, it->second, t, seen);
        }
        case Kind::Array:
            for (const Value& c : array_items(v)) if (reaches(g, c, t, seen)) return true;
            return false;
        case Kind::Object:
            for (const auto& e : object_entries(v)) if (reaches(g, e.second, t, seen)) return true;
            return false;
        case Kind::Struct:
            for (const auto& f : struct_fields(v)) if (reaches(g, f.second, t, seen)) return true;
            return false;
        default: return false;
    }
}

Value random_stored(TestGraph& g, std::vector<Value>& closures) {
    switch (below(5)) {
        case 0: return closures[below(closures.size())];
        case 1: return Value::handle(HandleKind::Mutex, below(4) + 1, 1);
        case 2: {
            Value a = make_array();
            array_push(a, I(1));
            array_push(a, below(2) ? closures[below(closures.size())] : Value::handle(HandleKind::Mutex, below(4) + 1, 1));
            Value o = make_object();
            object_set(o, atom("inner"), a);
            Value st = make_struct(atom("Box"));
            struct_add_field(st, atom("o"), o);
            return st;
        }
        case 3: { Value m = make_object(); object_set(m, atom("cb"), closures[below(closures.size())]); return m; }
        default: { Value plain = make_array(); array_push(plain, I(2)); (void)g; return plain; }
    }
}

void cycle_checker(long rounds) {
    long refused = 0, allowed = 0;
    for (long r = 0; r < rounds; ++r) {
        TestGraph g;
        g.cells.assign(6, Value());
        std::vector<Value> closures;
        for (int c = 0; c < 6; ++c) {
            Value f = Value::adopt(new FunctionObject());
            auto& cap = g.captures[f.heap_as<FunctionObject>()];
            const std::size_t n = below(3);
            for (std::size_t k = 0; k < n; ++k) cap.push_back(below(g.cells.size()));
            closures.push_back(f);
        }
        for (int step = 0; step < 25; ++step) {
            Value stored = random_stored(g, closures);
            const bool to_cell = below(2) == 0;
            const std::size_t cell = below(g.cells.size());
            const std::uint64_t res = below(4) + 1;
            const CycleTarget target = to_cell ? CycleTarget::captured_cell(&g.cells[cell])
                                               : CycleTarget::resource(Value::handle(HandleKind::Mutex, res, 1));
            std::set<const void*> seen;
            const bool expected = reaches(g, stored, target, seen);
            const bool got = would_close_cycle(target, stored, g);
            CHECK(got == expected);
            if (got) { ++refused; continue; }
            ++allowed;
            if (to_cell) g.cells[cell] = stored; else g.payloads[res] = stored;
        }
    }
    CHECK(refused > 0 && allowed > 0);
    std::printf("cycle checker: %ld stores refused, %ld allowed\n", refused, allowed);
}

void plain_stores_never_walk() {
    TestGraph g;
    g.cells.assign(1, Value());
    Value big = make_array();
    for (int n = 0; n < 10000; ++n) array_push(big, I(n));
    CycleCheckStats before = cycle_check_stats();
    for (int n = 0; n < 1000; ++n) CHECK(!would_close_cycle(CycleTarget::captured_cell(&g.cells[0]), big, g));
    CHECK(cycle_check_stats().checks == before.checks && cycle_check_stats().nodes_visited == before.nodes_visited);
    // A conservatively marked plain array is verified once, then skipped.
    array_mut(big)[0] = I(1);
    CHECK(may_contain_identity(big));
    CHECK(!would_close_cycle(CycleTarget::captured_cell(&g.cells[0]), big, g));
    CHECK(!may_contain_identity(big) && cycle_check_stats().summaries_cleared == before.summaries_cleared + 1);
    const std::uint64_t walked = cycle_check_stats().checks;
    CHECK(!would_close_cycle(CycleTarget::captured_cell(&g.cells[0]), big, g));
    CHECK(cycle_check_stats().checks == walked);
}

} // namespace

int main(int argc, char** argv) {
    const unsigned long seed = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 20261020ul;
    const long scale = argc > 2 ? std::strtol(argv[2], nullptr, 10) : 1;
    rng.seed(seed);
    structs();
    nested_cow(300 * scale);
    self_insertion();
    sameness();
    summary_bit(300 * scale);
    cycle_checker(400 * scale);
    plain_stores_never_walk();
    std::printf("seed %lu scale %ld\n", seed, scale);
    return rt_test::finish("rt value D-040");
}
