// D-040 value-layer tests (L7 step 2.1 and 2.7a; semantics.md V1-V19):
//
//   value_d040 [seed] [scale]
//
// 1. structs and collections are references (D-NEXT-refsem, superseding the
//    copy-on-write struct values of D-040 V9): shared writes, identity
//    equality and hash, self-stores found by the cycle checker
// 2. nested references against a reference model (B2, b2-refrep.md;
//    replaces nested copy-on-write against a deep-copy model): random
//    aliases and shallow copies, deep place writes through the protocol,
//    pushes and self-stores (refused cycles, receiver unchanged) over
//    several slots; after every step each slot equals its model by value
//    and identity and no value contains itself (acyclic, V16)
// 2b. self-insertion: arrays, objects and structs, two levels deep and
//    repeated; B2: a cycle the protocol refuses before anything changes
// 3. same(): identity for callables, handles, instances, collections
//    (D-NEXT-refsem #2) and (B2) arrays and objects, NotIdentity otherwise
// 4. the identity summary bit: sound (an aggregate with a callable, a handle
//    or a marked child below it is marked), precise for API stores of plain
//    data, conservative for raw array_mut
// 6. heap accounting: objects at and above the 512 MiB and 1 GiB boundaries,
//    several large objects, counter arithmetic (concern 3)
// 7. summary-bit metadata across aliases and explicit clones (B2)
// 8. tag-byte masking: with the bit set on every heap kind, kind, equality,
//    hash, rendering, serialization, legacy conversion, footprint, cloning and
//    destruction match the unmarked value
// 5. the cycle checker against brute-force reachability on random graphs of
//    captured cells, closures, handles and payloads; plain stores never walk;
//    stores into instances and collections (D-NEXT-refsem #4, aggregate targets)
#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/copy_graph.h"
#include "rt/heap.h"
#include "rt/mutation.h"
#include "rt/heap_account.h"
#include "rt/legacy_bridge.h"
#include "rt/natives/format_methods.h"
#include "rt/render.h"
#include "rt/error.h"
#include "RuntimeValue.h"
#include "rt/string.h"
#include "rt/value_cycle.h"
#include "rt_test.h"
#include "storage_access.h"  // B2: the storage commits (tests only)

#include <cstdlib>
#include <functional>
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

// ---- 1. structs and collections are references (D-NEXT-refsem #1, #2, #4) ----
// Superseding D-040 V9 (copy-on-write struct values, structural equality):
// every holder of an instance or a collection shares it, writes are in place,
// equality and hash of instances are identity, and a self-store is a cycle the
// caller must refuse (the checker finds it; the value layer never clones).
void structs() {
    Value q = make_struct(atom("Q"));
    struct_add_field(q, atom("v"), I(1));
    Value p = make_struct(atom("P"));
    struct_add_field(p, atom("x"), I(1));
    struct_add_field(p, atom("s"), I(2));  // a private field takes part like any other
    struct_add_field(p, atom("q"), q);
    Value copy = p;
    Value* inner = struct_slot_mut(copy, atom("q"));
    CHECK(inner && struct_set(*inner, atom("v"), I(7)));
    CHECK(copy.heap() == p.heap() && struct_get(*struct_get(p, atom("q")), atom("v"))->as_int() == 7);
    CHECK(struct_get(q, atom("v"))->as_int() == 7);  // the nested instance itself
    CHECK(equal(p, copy) && hash(p) == hash(copy));
    CHECK(!struct_set(copy, atom("nope"), I(1)) && !struct_slot_mut(copy, atom("nope")));
    Value twin = make_struct(atom("P"));  // same type and fields: another instance
    struct_add_field(twin, atom("x"), I(1));
    struct_add_field(twin, atom("s"), I(2));
    struct_add_field(twin, atom("q"), q);
    CHECK(!equal(p, twin) && !equal(twin, p));
    Value o = make_object();
    object_set(o, atom("x"), I(1));
    Value px = make_struct(atom("P"));
    struct_add_field(px, atom("x"), I(1));
    CHECK(!equal(px, o) && !equal(o, px));
    CHECK(copy.heap() == p.heap());  // never cloned (B2: no unshare at all)
    // A self-store is a cycle: found by the checker before anything is stored.
    CycleGraph graph;
    Value self = make_struct(atom("N"));
    struct_add_field(self, atom("next"), Value());
    CHECK(would_close_cycle(CycleTarget::aggregate(self.heap()), self, graph));
    CHECK(legacy_reference_cycle(self.heap(), self));
    Value holder = make_collection(CollectionKind::Map);
    map_set(holder, I(1), self);
    CHECK(would_close_cycle(CycleTarget::aggregate(self.heap()), holder, graph));
    CHECK(legacy_reference_cycle(self.heap(), holder));
    Value arr = make_array();
    array_push(arr, self);
    CHECK(would_close_cycle(CycleTarget::aggregate(self.heap()), arr, graph));
    CHECK(!legacy_reference_cycle(self.heap(), arr));  // legacy accepts it (arrays are copies there)
    CHECK(struct_get(self, atom("next"))->is_null());
    Value other = make_struct(atom("N"));
    CHECK(!would_close_cycle(CycleTarget::aggregate(self.heap()), other, graph));
    // Collections share the same way; a shallow clone is a new outer object.
    Value m = make_collection(CollectionKind::Map);
    Value alias = m;
    map_set(alias, I(1), I(2));
    CHECK(collection_size(m) == 1 && alias.heap() == m.heap());  // B2: no unshare at all
    Value clone = collection_shallow_clone(m);
    map_set(clone, I(3), I(4));
    CHECK(collection_size(m) == 1 && collection_size(clone) == 2 && clone.heap() != m.heap());
}

// ---- 2. nested references vs a reference model (B2) -------------------------
// Replaces nested copy-on-write vs a deep-copy model: B2 deletes the clone it
// tested. The model is a node graph: a holder of a node holds THE node.
struct Model {
    enum class K { Int, Arr, Obj } k = K::Int;
    std::int64_t i = 0;
    std::vector<int> items;                       // Arr: child ids (Int nodes for numbers)
    std::vector<std::pair<std::string, int>> kv;  // Obj
};
struct Graph {
    std::vector<Model> n;
    int add(Model m) { n.push_back(std::move(m)); return static_cast<int>(n.size()) - 1; }
    int num(std::int64_t i) { Model m; m.i = i; return add(m); }
    int arr() { Model m; m.k = Model::K::Arr; return add(m); }
    int obj() { Model m; m.k = Model::K::Obj; return add(m); }
    int& at(int node, const std::string& key) {
        for (auto& e : n[node].kv) if (e.first == key) return e.second;
        n[node].kv.emplace_back(key, -1);
        return n[node].kv.back().second;
    }
    bool reaches(int from, int target) const {
        if (from == target) return true;
        for (int c : n[from].items) if (reaches(c, target)) return true;
        for (const auto& e : n[from].kv) if (reaches(e.second, target)) return true;
        return false;
    }
};

// Value and identity: one heap object per aggregate node and vice versa.
bool matches(const Value& v, const Graph& g, int node, std::map<int, const HeapObject*>& by_node,
             std::map<const HeapObject*, int>& by_heap) {
    const Model& m = g.n[node];
    if (m.k == Model::K::Int) return v.is(Kind::Int) && v.as_int() == m.i;
    if (!(m.k == Model::K::Arr ? v.is(Kind::Array) : v.is(Kind::Object))) return false;
    const auto a = by_node.emplace(node, v.heap());
    const auto b = by_heap.emplace(v.heap(), node);
    if (a.first->second != v.heap() || b.first->second != node) return false;
    if (!a.second) return true;
    if (m.k == Model::K::Arr) {
        if (array_size(v) != m.items.size()) return false;
        for (std::size_t k = 0; k < m.items.size(); ++k)
            if (!matches(array_items(v)[k], g, m.items[k], by_node, by_heap)) return false;
        return true;
    }
    if (object_size(v) != m.kv.size()) return false;
    for (std::size_t k = 0; k < m.kv.size(); ++k)
        if (atom_text(object_entries(v)[k].first) != m.kv[k].first ||
            !matches(object_entries(v)[k].second, g, m.kv[k].second, by_node, by_heap))
            return false;
    return true;
}

// The document of the owner's case: an object holding arrays of objects.
Value base_doc(Graph& g, int& root) {
    Value items = make_array();
    const int mi = g.arr();
    for (int n = 0; n < 3; ++n) {
        Value item = make_object();
        object_set(item, atom("id"), I(n));
        Value tags = make_array();
        array_push(tags, I(10 * n));
        object_set(item, atom("tags"), tags);
        array_push(items, item);
        const int mo = g.obj(), mt = g.arr();
        g.at(mo, "id") = g.num(n);
        const int tag = g.num(10 * n);  // added before taking a reference into g.n
        g.n[mt].items.push_back(tag);
        g.at(mo, "tags") = mt;
        g.n[mi].items.push_back(mo);
    }
    Value meta = make_object();
    object_set(meta, atom("n"), I(3));
    const int mm = g.obj();
    g.at(mm, "n") = g.num(3);
    Value doc = make_object();
    object_set(doc, atom("items"), items);
    object_set(doc, atom("meta"), meta);
    root = g.obj();
    g.at(root, "items") = mi;
    g.at(root, "meta") = mm;
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

using mutation::Failure;
const mutation::Env kEnv;
Value member(const Value& o, const char* key) { return *object_get(o, atom(key)); }

void nested_references(long sequences) {
    constexpr int kSlots = 4;
    long refused = 0;
    for (long s = 0; s < sequences; ++s) {
        Graph g;
        std::vector<Value> slot(kSlots);
        std::vector<int> ms(kSlots);
        slot[0] = base_doc(g, ms[0]);
        for (int k = 1; k < kSlots; ++k) { slot[k] = slot[0]; ms[k] = ms[0]; }
        for (int step = 0; step < 40; ++step) {
            const int a = static_cast<int>(below(kSlots)), b = static_cast<int>(below(kSlots));
            const std::int64_t x = static_cast<std::int64_t>(below(1000));
            const std::size_t item = below(3);
            const int items_node = g.at(ms[a], "items");
            Value items = member(slot[a], "items");
            const std::size_t at = item % array_size(items);
            switch (below(8)) {
                case 0: slot[a] = slot[b]; ms[a] = ms[b]; break;  // alias: the same document
                case 1: {  // deep write doc["items"][item]["tags"][0] = x, through the protocol
                    Value tags = member(array_items(items)[at], "tags");
                    const int mt = g.at(g.n[items_node].items[at], "tags");
                    if (array_size(tags) > 0) {
                        CHECK(mutation::set(tags, 0, I(x), kEnv).ok());
                        g.n[mt].items[0] = g.num(x);
                    }
                    break;
                }
                case 2: {  // push into a nested array: doc["items"][item]["tags"].push(x)
                    Value tags = member(array_items(items)[at], "tags");
                    CHECK(mutation::push(tags, I(x), kEnv).ok());
                    const int added = g.num(x);  // added before taking a reference into g.n
                    g.n[g.at(g.n[items_node].items[at], "tags")].items.push_back(added);
                    break;
                }
                case 3: {  // a member through a path: doc.meta.n = x
                    Value meta = member(slot[a], "meta");
                    CHECK(mutation::object_put(meta, atom("n"), I(x), kEnv).ok());
                    g.at(g.at(ms[a], "meta"), "n") = g.num(x);
                    break;
                }
                case 4: {  // self-store doc["items"].push(doc["items"]): a cycle, refused, unchanged
                    const std::size_t before = array_size(items);
                    CHECK(mutation::push(items, items, kEnv).failure == Failure::Cycle);
                    CHECK(array_size(items) == before);
                    ++refused;
                    break;
                }
                case 5: {  // whole-document self-store under a new key: refused, unchanged
                    const std::size_t before = object_size(slot[a]);
                    CHECK(mutation::object_put(slot[a], atom("old"), slot[a], kEnv).failure == Failure::Cycle);
                    CHECK(object_size(slot[a]) == before);
                    ++refused;
                    break;
                }
                case 6: {  // a shallow copy: a new outer document sharing its members
                    Value copy;
                    CHECK(shallow_copy(slot[b], copy, kNoCopyLimit) == CopyStatus::Ok);
                    Model m = g.n[ms[b]];
                    slot[a] = copy;
                    ms[a] = g.add(m);
                    break;
                }
                default: {  // repeated writes to one shared item
                    Value it = array_items(items)[at];
                    for (int rep = 0; rep < 3; ++rep) CHECK(mutation::object_put(it, atom("id"), I(x + rep), kEnv).ok());
                    g.at(g.n[items_node].items[at], "id") = g.num(x + 2);
                    break;
                }
            }
            std::map<int, const HeapObject*> by_node;
            std::map<const HeapObject*, int> by_heap;
            for (int k = 0; k < kSlots; ++k) {
                CHECK(matches(slot[k], g, ms[k], by_node, by_heap));
                std::vector<const HeapObject*> path;
                CHECK(acyclic(slot[k], path));
            }
            for (int k = 0; k < kSlots; ++k) CHECK(!g.reaches(g.at(ms[k], "items"), ms[k]));
        }
    }
    std::printf("nested references: %ld refused cycle-forming stores\n", refused);
}


// ---- 2b. self-insertion (owner requirement). B2 (b2-refrep.md): arrays and
// objects are references, so a self-insertion is a cycle the protocol refuses
// before anything changes (replaces "the copy-on-write detach stores the
// pre-write snapshot": the clone is deleted). No graph cycle is ever built.
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
    // a = [1]; a.push(a): refused, a unchanged; later writes work.
    Value a = make_array();
    array_push(a, I(1));
    CHECK(mutation::push(a, a, kEnv).failure == Failure::Cycle);
    CHECK(show(a) == "[1]" && acyclic_value(a) && a.refcount() == 1);
    CHECK(mutation::push(a, I(2), kEnv).ok() && show(a) == "[1,2]");
    // Repeated self-insertion: every attempt is refused.
    for (int n = 0; n < 5; ++n) CHECK(mutation::push(a, a, kEnv).failure == Failure::Cycle && show(a) == "[1,2]");
    // Two levels deep, array: a[0][0].push(a) would make a reach itself.
    Value d = make_array();
    { Value inner = make_array(); Value leaf = make_array(); array_push(leaf, I(1)); array_push(inner, leaf);
      array_push(d, inner); }
    Value leaf = array_items(array_items(d)[0])[0];
    CHECK(mutation::push(leaf, d, kEnv).failure == Failure::Cycle);
    CHECK(show(d) == "[[[1]]]" && acyclic_value(d));
    // Objects, two levels: o["k"]["m"].push(o) and o["self"] = o.
    Value o = make_object();
    { Value k = make_object(); Value m = make_array(); array_push(m, I(1)); object_set(k, atom("m"), m);
      object_set(o, atom("k"), k); }
    Value m = *object_get(*object_get(o, atom("k")), atom("m"));
    CHECK(mutation::push(m, o, kEnv).failure == Failure::Cycle);
    CHECK(mutation::object_put(o, atom("self"), o, kEnv).failure == Failure::Cycle);
    CHECK(show(o) == "{k:{m:[1]}}" && acyclic_value(o));
    CHECK(mutation::push(m, I(5), kEnv).ok() && show(o) == "{k:{m:[1,5]}}");  // in place, seen through o
    // Structs (D-NEXT-refsem #4): a store of an instance into itself, directly
    // or two levels down, is a cycle the checker finds before the caller
    // stores anything.
    Value s = make_struct(atom("S"));
    { Value inner = make_struct(atom("In")); struct_add_field(inner, atom("list"), make_array());
      struct_add_field(s, atom("inner"), inner); struct_add_field(s, atom("next"), Value()); }
    CycleGraph graph;
    const HeapObject* list_owner = struct_get(s, atom("inner"))->heap();
    CHECK(would_close_cycle(CycleTarget::aggregate(s.heap()), s, graph));
    CHECK(would_close_cycle(CycleTarget::aggregate(list_owner), s, graph));
    CHECK(show(s) == "S{inner:In{list:[]},next:?}" && acyclic_value(s));
    // Self-insertion through an alias another slot holds: the same object, refused.
    Value x = make_array();
    array_push(x, I(1));
    Value y = x;
    CHECK(mutation::push(x, y, kEnv).failure == Failure::Cycle && show(x) == "[1]" && show(y) == "[1]");
    // A direct self-store is refused in O(1), without a walk.
    CycleCheckStats before_stats = cycle_check_stats();
    CHECK(mutation::push(a, a, kEnv).failure == Failure::Cycle);
    CHECK(mutation::object_put(o, atom("again"), o, kEnv).failure == Failure::Cycle);
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
    // B2 (plan section 3): arrays and objects are references, so same() is
    // their identity (replaces "storage sharing is never exposed").
    Value a = make_array();
    Value b = a;
    Value m = make_object();
    Value twin = make_array();
    CHECK(same(a, b) == Sameness::Same && same(a, a) == Sameness::Same && same(m, m) == Sameness::Same);
    CHECK(same(a, twin) == Sameness::Different && equal(a, twin));  // equal content, distinct identity
    CHECK(same(a, m) == Sameness::Different && same(a, f) == Sameness::Different);
    Value copy;
    CHECK(shallow_copy(a, copy, kNoCopyLimit) == CopyStatus::Ok && same(a, copy) == Sameness::Different);
    CHECK(same(I(1), I(1)) == Sameness::NotIdentity && same(make_string("x"), h) == Sameness::NotIdentity);
    // D-NEXT-refsem #2: struct instances and collections are identities.
    Value st = make_struct(atom("P"));
    Value st2 = st;
    Value other = make_struct(atom("P"));
    Value c = make_collection(CollectionKind::Set);
    CHECK(same(st, st2) == Sameness::Same && same(st, other) == Sameness::Different);
    CHECK(same(c, c) == Sameness::Same && same(c, st) == Sameness::Different);
    CHECK(identity_bearing(f) && identity_bearing(h) && !identity_bearing(a) && identity_bearing(st) &&
          identity_bearing(c));  // identity_bearing: the D-040 identity kinds (arrays: references, B2)
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
        // Arrays and objects (B2: references; a store that would close a cycle
        // is refused by the protocol, the receiver unchanged); instances:
        // section 1.
        std::vector<Value> pool = {make_array(), make_object()};
        for (int step = 0; step < 30; ++step) {
            Value item;
            switch (below(6)) {
                case 0: item = Value::adopt(new FunctionObject()); break;
                case 1: item = Value::handle(HandleKind::Timer, below(5) + 1, 1); break;
                case 2: item = pool[below(pool.size())]; break;
                default: item = I(static_cast<std::int64_t>(below(100))); break;
            }
            Value& target = pool[below(pool.size())];
            const mutation::Outcome out = target.is(Kind::Array)
                ? mutation::push(target, item, kEnv)
                : mutation::object_put(target, atom("k" + std::to_string(below(4))), item, kEnv);
            CHECK(out.ok() || out.failure == Failure::Cycle);
            if (below(4) == 0) pool.push_back(target);  // an alias (B2: the same object)
        }
        for (const Value& v : pool) CHECK(!contains_identity(v) || may_contain_identity(v));  // sound
    }
    // Precise for API stores of plain data; conservative for raw storage.
    // B2 (plan 5.1): a nested array or object marks its parent (it is a
    // reference); a flat array of scalars stays clear.
    Value plain = make_array();
    for (int n = 0; n < 50; ++n) array_push(plain, I(n));
    Value obj = make_object();
    object_set(obj, atom("a"), plain);
    Value st = make_struct(atom("S"));
    struct_add_field(st, atom("o"), obj);
    array_set(plain, 0, I(5));
    CHECK(!may_contain_identity(plain) && may_contain_identity(obj));
    CHECK(may_contain_identity(st) && may_hold_identity(st.heap()));  // an identity itself, holding an object
    array_mut(plain)[1] = I(6);
    CHECK(may_contain_identity(plain));  // raw access: marked conservatively
    Value copy = array_shallow_clone(plain);
    CHECK(may_contain_identity(copy));  // explicit clones keep the bit
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
    if (t.kind == CycleTarget::Kind::Aggregate && v.is_heap() && v.heap() == t.cell) return true;  // D-NEXT-refsem
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
            if (!seen.insert(v.heap()).second) return false;
            for (const auto& f : struct_fields(v)) if (reaches(g, f.second, t, seen)) return true;
            return false;
        case Kind::Collection:
            if (!seen.insert(v.heap()).second) return false;
            for (const Value& c : collection_items(v)) if (reaches(g, c, t, seen)) return true;
            return false;
        default: return false;
    }
}

// D-NEXT-refsem #4: stores into instances and collections (the reference
// kinds) against brute-force reachability, through arrays, objects, other
// instances and collections, and closures whose captured cells hold them.
// A store the checker allows is made, so later graphs are deeper; a refused
// one leaves everything as it was.
void aggregate_cycles(long rounds) {
    long refused = 0, allowed = 0;
    for (long r = 0; r < rounds; ++r) {
        TestGraph g;
        g.cells.assign(4, Value());
        std::vector<Value> refs;
        for (int k = 0; k < 3; ++k) refs.push_back(make_collection(CollectionKind::Map));
        refs.push_back(make_collection(CollectionKind::Stack));
        for (int k = 0; k < 2; ++k) {
            Value st = make_struct(atom("N"));
            struct_add_field(st, atom("next"), Value());
            refs.push_back(st);
        }
        std::vector<Value> closures;
        for (int c = 0; c < 4; ++c) {
            Value f = Value::adopt(new FunctionObject());
            g.captures[f.heap_as<FunctionObject>()].push_back(static_cast<std::size_t>(c));
            closures.push_back(f);
        }
        for (int step = 0; step < 30; ++step) {
            const Value& pick = refs[below(refs.size())];
            Value stored;
            switch (below(5)) {
                case 0: stored = pick; break;
                case 1: stored = make_array(); array_push(stored, I(1)); array_push(stored, pick); break;
                case 2: stored = make_object(); object_set(stored, atom("k"), pick); break;
                case 3: stored = closures[below(closures.size())]; break;
                default: stored = I(static_cast<std::int64_t>(below(9))); break;
            }
            if (below(4) == 0) {  // a captured variable takes a reference (cells are not checked here)
                g.cells[below(g.cells.size())] = refs[below(refs.size())];
                continue;
            }
            Value& target = refs[below(refs.size())];
            const CycleTarget t = CycleTarget::aggregate(target.heap());
            std::set<const void*> seen;
            const bool expected = reaches(g, stored, t, seen);
            const std::size_t before = target.is(Kind::Struct) ? 1 : collection_size(target);
            const bool got = would_close_cycle(t, stored, g);
            CHECK(got == expected);
            if (got) {
                ++refused;
                CHECK((target.is(Kind::Struct) ? 1 : collection_size(target)) == before);
                continue;
            }
            ++allowed;
            if (target.is(Kind::Struct)) struct_set(target, atom("next"), stored);
            else if (collection_kind(target) == CollectionKind::Stack) stack_push(target, stored);
            else map_set(target, I(static_cast<std::int64_t>(below(4))), stored);
        }
        // Break every reference so the round's graph is freed (acyclic by the checks).
        for (Value& c : g.cells) c = Value();
        for (Value& v : refs)
            if (v.is(Kind::Struct)) struct_set(v, atom("next"), Value()); else collection_clear(v);
    }
    CHECK(refused > 0 && allowed > 0);
    std::printf("aggregate cycle checker: %ld stores refused, %ld allowed\n", refused, allowed);
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


// ---- 6. heap accounting of large objects (memory investigation concern 3) ----
// The header field holds 2^24 - 1 granules (1 GiB - 64 bytes); objects beyond
// it are counted exactly in a side table. Whatever the size, the live total
// (which the VM heap budget reads) must equal the sum of the exact footprints.
// Storage is reserved, not touched; runs in test-rt-value under ulimit -v.
// The ASan build skips it (argument `no-large`): ASan reserves its shadow
// memory up front and cannot run under ulimit -v, and these address-space
// tests need the non-ASan allocator and the limit.
Value big_string(std::size_t capacity) {
    auto* so = new StringObject(std::string());
    so->text.reserve(capacity);
    heap_retally(so);
    return Value::adopt(so);
}

void large_accounting() {
    const std::size_t base = live_heap_bytes();
    const std::size_t mib = std::size_t(1) << 20, gib = std::size_t(1) << 30;
    // Single objects at and just above the 512 MiB and 1 GiB boundaries.
    for (std::size_t bytes : {512 * mib - 64, 512 * mib, 512 * mib + 1, gib - 256, gib - 64, gib, gib + 1, gib + 64,
                              3 * gib / 2}) {
        Value v = big_string(bytes);
        const std::size_t footprint = heap_footprint(v.heap());
        CHECK(footprint >= bytes && footprint % kHeapGranule == 0);
        CHECK(live_heap_bytes() - base == footprint);  // exact, also beyond the 1 GiB field
    }
    CHECK(live_heap_bytes() == base);
    // Several large objects at once: the total is their sum, never capped.
    {
        Value a = big_string(gib + mib), b = big_string(gib / 4), c = big_string(gib / 4);
        const std::size_t sum = heap_footprint(a.heap()) + heap_footprint(b.heap()) + heap_footprint(c.heap());
        CHECK(live_heap_bytes() - base == sum && sum > gib + gib / 2);
        // Transitions: above -> below the field (leaves the side table), below -> above, above -> above.
        std::string().swap(a.heap_as<StringObject>()->text);
        heap_retally(a.heap());
        CHECK(live_heap_bytes() - base == heap_footprint(a.heap()) + heap_footprint(b.heap()) + heap_footprint(c.heap()));
        CHECK(heap_saturated_objects() == 0);  // a left the side table
        b.heap_as<StringObject>()->text.reserve(gib + 2 * mib);
        heap_retally(b.heap());
        c.heap_as<StringObject>()->text.reserve(gib + 4 * mib);
        heap_retally(c.heap());
        heap_retally(c.heap());  // retally without change: no drift
        CHECK(live_heap_bytes() - base == heap_footprint(a.heap()) + heap_footprint(b.heap()) + heap_footprint(c.heap()));
        CHECK(live_heap_bytes() - base > 2 * gib && heap_saturated_objects() == 2);
    }
    CHECK(heap_saturated_objects() == 0);
    CHECK(live_heap_bytes() == base);  // destruction removes the exact amounts (side table included)
    // Counter arithmetic: shrinking objects subtract (modular difference, never
    // underflowing the total), untallied objects count 0, re-tally is idempotent.
    {
        Value s = big_string(64 * mib);
        const std::size_t with = live_heap_bytes();
        std::string().swap(s.heap_as<StringObject>()->text);
        heap_retally(s.heap());
        CHECK(live_heap_bytes() < with && live_heap_bytes() - base == heap_footprint(s.heap()));
        heap_retally(s.heap());
        CHECK(live_heap_bytes() - base == heap_footprint(s.heap()));
    }
    CHECK(live_heap_bytes() == base);
}

// ---- 7. summary-bit metadata across aliases and explicit clones (B2) ----------
// Behaviour chosen: the bit is conservative. It is copied by explicit clones,
// set by any store that may add a callable, a handle or (B2) a nested array or
// object, and may stay set after the last one is removed (cleared only when
// the cycle checker verifies the children); it is never missing while one is
// present. Replaces the copy-on-write version (aliases now share one bit).
void cow_metadata() {
    Value a = make_array();
    array_push(a, I(1));
    Value b = a;  // an alias: the same array, one bit
    CHECK(mutation::push(a, Value::adopt(new FunctionObject()), kEnv).ok());  // a.push(fn() { return 1 })
    CHECK(a.heap() == b.heap() && may_contain_identity(a) && may_contain_identity(b) && array_size(b) == 2);
    // A cycle-producing store involving a is refused and leaves a unchanged.
    TestGraph g;
    g.cells.assign(1, Value());
    auto* f = new FunctionObject();
    Value closure = Value::adopt(f);
    g.captures[f] = {0};
    g.cells[0] = a;
    Value holder = array_shallow_clone(a);
    array_push(holder, closure);  // holder = a's elements + [closure capturing cell 0 (a)]
    CHECK(would_close_cycle(CycleTarget::captured_cell(&g.cells[0]), holder, g));
    CHECK(would_close_cycle(CycleTarget::aggregate(a.heap()), closure, g));
    CHECK(array_size(a) == 2 && may_contain_identity(holder));
    g.cells[0] = Value();
    // Removing the last callable or handle: the bit may stay set, never missing.
    Value c = make_array();
    array_push(c, Value::handle(HandleKind::File, 1, 1));
    array_push(c, I(2));
    array_mut(c).erase(array_mut(c).begin());  // the handle is gone
    CHECK(may_contain_identity(c));            // conservative: still set
    CycleGraph none;
    CHECK(!would_close_cycle(CycleTarget::captured_cell(&g.cells[0]), c, none));
    CHECK(!may_contain_identity(c));           // verified clean by the checker
    array_push(c, Value::adopt(new FunctionObject()));
    CHECK(may_contain_identity(c));            // never missing when one is present
    Value d = array_shallow_clone(c);          // an explicit clone keeps the bit
    array_mut(d).pop_back();
    CHECK(may_contain_identity(c) && array_size(c) == 2 && d.heap() != c.heap() && may_contain_identity(d));
}


// ---- 8. tag-byte masking: the summary bit never changes what a value is -------
// The bit shares the header's tag byte with the Kind. Every reader of the
// header kind masks it (HeapObject::kind()); everything else reads the Value's
// own kind byte, which is set from the masked kind. Set the bit on every heap
// kind and check that kind, equality, hash, truthiness, rendering,
// serialization, legacy conversion, footprint, cloning and destruction all
// match the unmarked twin.
struct Twin { Value plain, marked; bool aggregate; };

Value with_bit(Value v) {
    if (v.is_heap()) mark_may_hold_identity(v.heap());
    return v;
}

std::string serialized(const Value& v) {
    std::string out, error;
    bool internal = false;
    const bool ok = nift::rt::natives::serialize(v, false, out, error, internal);
    return (ok ? "ok:" : "no:") + out + "|" + error + (internal ? "|internal" : "");
}

std::string legacy_text(const Value& v) {
    nift::RuntimeValue out;
    std::string error;
    if (!to_legacy(v, out, error)) return "error:" + error;
    try {
        return out.dump();
    } catch (const std::exception& e) {
        return std::string("throws:") + e.what();
    }
}

void tag_masking() {
    std::vector<std::pair<const char*, std::function<Value()>>> makers = {
        {"string", [] { return make_string("a heap string longer than fourteen bytes"); }},
        {"decimal", [] { return make_decimal("123456789012345678901234567890.5", 1.2345678901234568e29); }},
        {"bytes", [] { return make_bytes({1, 2, 3}); }},
        {"array", [] { Value a = make_array(); array_push(a, I(1)); array_push(a, make_string("x")); return a; }},
        {"object", [] { Value o = make_object(); object_set(o, atom("k"), I(2)); return o; }},
        {"struct", [] { Value s = make_struct(atom("P")); struct_add_field(s, atom("x"), I(3)); return s; }},
        {"error", [] { return make_user_error("boom"); }},
    };
    for (auto& [name, make] : makers) {
        Value plain = make(), marked = with_bit(make());
        CHECK(marked.kind() == plain.kind() && marked.heap()->kind() == plain.kind());
        if (std::string(name) == "struct")  // D-NEXT-refsem #2: two instances are never equal; each is itself
            CHECK(!equal(plain, marked) && equal(marked, marked) && hash(marked) == hash(marked));
        else
            CHECK(equal(plain, marked) && equal(marked, plain) && hash(plain) == hash(marked));
        CHECK(truthy(plain) == truthy(marked));
        std::string a, b;
        CHECK(append_rendered(plain, a) == append_rendered(marked, b) && a == b);
        CHECK(serialized(plain) == serialized(marked));
        CHECK(legacy_text(plain) == legacy_text(marked));
        CHECK(heap_footprint(plain.heap()) == heap_footprint(marked.heap()));
        CHECK(std::string(kind_name(marked.kind())) == kind_name(plain.kind()));
        if (!(std::string(name) == "array" || std::string(name) == "object" || std::string(name) == "struct")) continue;
        // Cloning a marked aggregate: the clone has the right kind and keeps the
        // bit (B2: an explicit shallow clone; a write goes to the shared object).
        Value share = marked;
        Value clone = marked;
        if (clone.is(Kind::Array)) { clone = array_shallow_clone(marked); array_push(clone, I(9)); }
        if (clone.is(Kind::Object)) { clone = object_shallow_clone(marked); object_set(clone, atom("z"), I(9)); }
        if (clone.is(Kind::Struct)) {  // D-NEXT-refsem #1: a reference, written in place, never cloned
            // (B2: the receiver form; struct_add_field is construction on a fresh instance)
            CHECK(struct_set(clone, atom("x"), I(9)));
            CHECK(clone.heap() == share.heap() && may_hold_identity(clone.heap()));
            continue;
        }
        CHECK(clone.heap() != share.heap() && clone.kind() == plain.kind() && clone.heap()->kind() == plain.kind());
        CHECK(may_hold_identity(clone.heap()) && equal(share, plain));
    }
    // Functions: identity values; the bit (never set by stores) changes nothing either.
    Value f = Value::adopt(new FunctionObject());
    Value g = with_bit(f);
    CHECK(g.kind() == Kind::Function && f.heap()->kind() == Kind::Function && equal(f, g) && same(f, g) == Sameness::Same);
    // adopt() of an object whose bit is already set yields the right kind.
    auto* raw = new ArrayObject();
    mark_may_hold_identity(raw);
    Value adopted = Value::adopt(raw);
    CHECK(adopted.kind() == Kind::Array && array_size(adopted) == 0 && may_contain_identity(adopted));
    // Destruction of marked objects of every kind (ASan/UBSan check the frees).
    for (auto& [name, make] : makers) { Value v = with_bit(make()); (void)name; }
    CHECK(true);
}

} // namespace

int main(int argc, char** argv) {
    const unsigned long seed = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 20261020ul;
    const long scale = argc > 2 ? std::strtol(argv[2], nullptr, 10) : 1;
    const bool large = !(argc > 3 && std::string(argv[3]) == "no-large");
    rng.seed(seed);
    structs();
    nested_references(300 * scale);
    self_insertion();
    sameness();
    summary_bit(300 * scale);
    cycle_checker(400 * scale);
    aggregate_cycles(300 * scale);
    plain_stores_never_walk();
    if (large) large_accounting();
    else std::printf("large_accounting skipped (no-large)\n");
    cow_metadata();
    tag_masking();
    std::printf("seed %lu scale %ld\n", seed, scale);
    return rt_test::finish("rt value D-040");
}
