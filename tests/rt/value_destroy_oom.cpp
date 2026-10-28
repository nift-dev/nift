// Memory investigation C1 (permanent regression; original reproducer:
// memcheck-repro tests/rt/memcheck/destroy_oom.cpp). Releasing the last
// reference to an aggregate must not need memory: destroy_heap_object runs
// inside noexcept paths (~Value, operator=), so an allocation failure there
// used to be std::terminate (SIGABRT 18/24 in the bounded ulimit -v matrix)
// instead of the VM's "memory budget exceeded" error.
//
// This binary replaces operator new with one that can be told to fail (all
// allocations, or every one after the next k). Each case builds a value
// graph, starts failing allocations, drops the last reference and checks that
// (a) the process survives, (b) every object was freed (live allocation count
// and live heap bytes return to their pre-build values), (c) values still
// referenced from outside survive intact. With the pre-fix destroy (a
// std::vector worklist) the first case that pushes a dying aggregate aborts.
//
// Usage: value-destroy-oom [case]   (no argument: every case)
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/heap_account.h"
#include "rt/proto.h"
#include "rt/cell.h"
#include "rt/string.h"
#include "rt/value.h"
#include "rt_test.h"

#ifdef NIFT_RT_DESTROY_LOCAL
#define NIFT_RT_DESTROY_LOCAL_FOR_TEST NIFT_RT_DESTROY_LOCAL
#else
#define NIFT_RT_DESTROY_LOCAL_FOR_TEST 32
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {
std::atomic<bool> g_fail{false};
std::atomic<long> g_allow{0};      // allocations still allowed once failing
std::atomic<long> g_failed{0};     // allocations refused
std::atomic<long> g_live{0};       // operator new minus operator delete
// Property tests: while g_inject is set, allocation number g_calls (counted
// from 0) fails if g_fail_at <= it (g_fail_only: only exactly g_fail_at).
std::atomic<bool> g_inject{false};
std::atomic<bool> g_fail_only{false};
std::atomic<long> g_fail_at{-1};
std::atomic<long> g_calls{0};
} // namespace

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"  // malloc/free pairs inside the replacements
#endif

void* operator new(std::size_t n) {
    if (g_fail.load(std::memory_order_relaxed) && g_allow.fetch_sub(1, std::memory_order_relaxed) <= 0) {
        g_failed.fetch_add(1, std::memory_order_relaxed);
        throw std::bad_alloc();
    }
    if (g_inject.load(std::memory_order_relaxed)) {
        const long call = g_calls.fetch_add(1, std::memory_order_relaxed);
        const long at = g_fail_at.load(std::memory_order_relaxed);
        if (at >= 0 && (g_fail_only.load(std::memory_order_relaxed) ? call == at : call >= at)) {
            g_failed.fetch_add(1, std::memory_order_relaxed);
            throw std::bad_alloc();
        }
    }
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    g_live.fetch_add(1, std::memory_order_relaxed);
    return p;
}
void operator delete(void* p) noexcept {
    if (p) g_live.fetch_sub(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

using namespace nift::rt;

namespace {

std::string long_text(const char* prefix, std::size_t i) {
    return std::string(prefix) + "-longer-than-fourteen-bytes-" + std::to_string(i);
}

struct Baseline {
    long allocs = g_live.load();
    std::size_t bytes = live_heap_bytes();
};

// Drops `root` while allocations fail (after `allow` more succeed); checks
// everything built since `base` was freed. Returns allocations refused.
long release_failing(Value& root, const Baseline& base, long allow, const char* name) {
    g_failed = 0;
    g_allow = allow;
    g_fail = true;
    root = Value();  // noexcept: release -> destroy_heap_object
    g_fail = false;
    CHECK(g_live.load() == base.allocs);
    CHECK(live_heap_bytes() == base.bytes);
    if (g_live.load() != base.allocs || live_heap_bytes() != base.bytes)
        std::fprintf(stderr, "  %s: live allocs %ld (expected %ld), heap bytes %zu (expected %zu)\n", name,
                     g_live.load(), base.allocs, live_heap_bytes(), base.bytes);
    return g_failed.load();
}

// a = [a] chain: one dying aggregate per level (stays in the in-frame buffer).
Value nested_chain(std::size_t depth) {
    Value root = make_array(0);
    for (std::size_t i = 0; i < depth; ++i) {
        Value outer = make_array(1);
        array_push(outer, root);
        root = outer;
    }
    return root;
}

// `width` dying aggregates under one parent (more than the 32-slot buffer:
// the rest spill to the heap vector, or go to pointer reversal if it fails).
Value wide(std::size_t width) {
    Value root = make_array(width);
    for (std::size_t i = 0; i < width; ++i) {
        Value a = make_array(1);
        array_push(a, make_string(long_text("inner", i)));
        Value b = make_array(1);
        array_push(b, a);
        array_push(root, b);
    }
    return root;
}

// Mixed kinds along a deep spine: each level is an object, struct or array
// holding the next level plus leaves, so pointer reversal walks every kind and
// resumes at non-zero child indexes.
Value spine(std::size_t depth, std::size_t salt) {
    const Atom next = atom("next"), leaf = atom("leaf"), num = atom("num"), type = atom("Node");
    Value cur = make_array(0);
    for (std::size_t i = 0; i < depth; ++i) {
        Value level;
        switch ((i + salt) % 3) {
            case 0:
                level = make_object();
                object_set(level, leaf, make_string(long_text("o", i)));
                object_set(level, num, Value::integer(static_cast<std::int64_t>(i)));
                object_set(level, next, cur);
                break;
            case 1:
                level = make_struct(type, 3);
                struct_add_field(level, leaf, make_string(long_text("s", i)));
                struct_add_field(level, next, cur);
                struct_add_field(level, num, make_array(0));  // a dying aggregate after `next`
                break;
            default:
                level = make_array(3);
                array_push(level, make_string(long_text("a", i)));
                array_push(level, Value::integer(1));
                array_push(level, cur);
                break;
        }
        cur = level;
    }
    return cur;
}

// Complete tree, every node an array of `fan` aggregate children.
Value bushy(std::size_t depth, std::size_t fan) {
    if (depth == 0) {
        Value leafv = make_array(1);
        array_push(leafv, make_string(long_text("leaf", fan)));
        return leafv;
    }
    Value node = make_array(fan);
    for (std::size_t i = 0; i < fan; ++i) array_push(node, bushy(depth - 1, fan));
    return node;
}


// ---- Property: failure injected at every allocation index (small graphs) ----
//
// A random small DAG with shared copy-on-write children (Value copies of the
// same object, COW clones that share their children with the original, the
// same child several times in one parent) and outside references to random
// nodes. Built twice from the same seed: once to count the allocations the
// release makes, then once per injection policy (fail every allocation from
// index k on; fail only allocation k) for every k. After each release the
// survivors are checked against the model: exact labels and child order,
// exact refcounts (outside handles plus edges from surviving objects), exact
// live heap bytes (base plus the footprint of every surviving object) and,
// once the outside references are dropped too, exact live allocation count.
// Under ASan any double free or dangling child is a hard error.
} // namespace
namespace prop {

struct Node {
    int kind = 0;                // 0 array, 1 object, 2 struct
    std::vector<int> children;   // node ids, in slot order
    int copy_of = -1;            // COW clone of this node plus one extra child
};

struct Graph {
    std::vector<Node> nodes;
    std::vector<int> roots;      // children of the released root (may repeat)
    std::vector<int> kept;       // outside references (may repeat)
};

Graph generate(std::uint32_t seed) {
    std::mt19937 rng(seed);
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<std::uint32_t>(n)); };
    Graph g;
    const int n = 2 + pick(9);
    for (int i = 0; i < n; ++i) {
        Node node;
        node.kind = pick(3);
        if (i > 0 && pick(4) == 0) {
            node.copy_of = pick(i);
            node.kind = g.nodes[node.copy_of].kind;
            node.children = g.nodes[node.copy_of].children;
            node.children.push_back(pick(i));
        } else {
            const int fan = i == 0 ? 0 : pick(4);
            for (int c = 0; c < fan; ++c) node.children.push_back(pick(i));
        }
        g.nodes.push_back(node);
    }
    const int nroots = 1 + pick(4);
    for (int r = 0; r < nroots; ++r) g.roots.push_back(n - 1 - pick(std::min(n, 4)));
    const int nkept = pick(3);
    for (int k = 0; k < nkept; ++k) g.kept.push_back(pick(n));
    return g;
}

Atom key(std::size_t slot) { return atom("k" + std::to_string(slot)); }

std::string label(int id) { return "node-" + std::to_string(id) + "-label-longer-than-fourteen"; }

// Slot 0 is the label; children follow in order.
void add_slot(Value& v, int kind, std::size_t slot, Value child) {
    if (kind == 0) array_push(v, std::move(child));
    else if (kind == 1) object_set(v, key(slot), std::move(child));
    else struct_add_field(v, key(slot), std::move(child));
}

std::vector<Value> build(const Graph& g) {
    std::vector<Value> built;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const Node& node = g.nodes[i];
        if (node.copy_of >= 0) {
            Value v = built[node.copy_of];  // shares the object
            const std::size_t slot = node.children.size();
            add_slot(v, node.kind, slot, built[node.children.back()]);  // COW clone sharing every child
            // relabel slot 0 (the clone shares its label string until now)
            if (node.kind == 0) array_set(v, 0, make_string(label(int(i))));
            else if (node.kind == 1) object_set(v, key(0), make_string(label(int(i))));
            else struct_set(v, key(0), make_string(label(int(i))));
            built.push_back(v);
            continue;
        }
        Value v = node.kind == 0 ? make_array(0) : node.kind == 1 ? make_object() : make_struct(atom("P"), 0);
        add_slot(v, node.kind, 0, make_string(label(int(i))));
        for (std::size_t c = 0; c < node.children.size(); ++c) add_slot(v, node.kind, c + 1, built[node.children[c]]);
        built.push_back(v);
    }
    return built;
}

std::vector<const Value*> slots(const Value& v) {
    std::vector<const Value*> out;
    if (v.is(Kind::Array)) for (const Value& c : array_items(v)) out.push_back(&c);
    else if (v.is(Kind::Object)) for (const auto& e : object_entries(v)) out.push_back(&e.second);
    else if (v.is(Kind::Struct)) for (const auto& f : struct_fields(v)) out.push_back(&f.second);
    return out;
}

// Checks `v` is node `id` (label, kind, children recursively); collects the
// distinct surviving objects and the edges between them.
void verify(const Graph& g, const Value& v, int id, std::map<const HeapObject*, long>& incoming,
            std::set<const HeapObject*>& seen) {
    const Node& node = g.nodes[id];
    const Kind want = node.kind == 0 ? Kind::Array : node.kind == 1 ? Kind::Object : Kind::Struct;
    CHECK(v.is(want));
    if (!v.is(want)) return;
    const auto s = slots(v);
    CHECK(s.size() == node.children.size() + 1);
    if (s.size() != node.children.size() + 1) return;
    CHECK(s[0]->is(Kind::String) && string_view_of(*s[0]) == label(id));
    if (!seen.insert(v.heap()).second) return;  // edges already counted once for this object
    if (s[0]->is_heap()) ++incoming[s[0]->heap()], seen.insert(s[0]->heap());
    for (std::size_t c = 0; c < node.children.size(); ++c) {
        const Value& child = *s[c + 1];
        if (child.is_heap()) ++incoming[child.heap()];
        verify(g, child, node.children[c], incoming, seen);
    }
}

void check_survivors(const Graph& g, const std::vector<Value>& kept, const Baseline& base) {
    std::map<const HeapObject*, long> incoming;
    std::set<const HeapObject*> seen;
    for (std::size_t k = 0; k < kept.size(); ++k) {
        if (kept[k].is_heap()) ++incoming[kept[k].heap()];
        verify(g, kept[k], g.kept[k], incoming, seen);
    }
    std::size_t bytes = base.bytes;
    for (const HeapObject* o : seen) {
        bytes += heap_footprint(o);
        CHECK(o->refcount == static_cast<std::uint32_t>(incoming[o]));
    }
    CHECK(live_heap_bytes() == bytes);
}

struct Totals {
    long graphs = 0, releases = 0, injected = 0;
};

void run_graph(std::uint32_t seed, Totals& totals) {
    const Graph g = generate(seed);
    auto once = [&](long fail_at, bool only) -> long {
        Baseline base;
        long calls;
        {
            std::vector<Value> built = build(g);
            Value root = make_array(0);
            for (int r : g.roots) array_push(root, built[r]);
            std::vector<Value> kept;
            for (int k : g.kept) kept.push_back(built[k]);
            built.clear();
            g_failed = 0;
            g_calls = 0;
            g_fail_only = only;
            g_fail_at = fail_at;
            g_inject = true;
            root = Value();
            g_inject = false;
            calls = g_calls.load();
            totals.injected += g_failed.load();
            ++totals.releases;
            check_survivors(g, kept, base);
        }
        CHECK(g_live.load() == base.allocs);
        CHECK(live_heap_bytes() == base.bytes);
        return calls;
    };
    const long calls = once(-1, false);  // reference run: allocations the release makes
    for (long k = 0; k <= calls; ++k) once(k, false);
    for (long k = 0; k < calls; ++k) once(k, true);
    ++totals.graphs;
}

} // namespace prop
namespace {

bool want(const char* only, const char* name) { return !only || std::strcmp(only, name) == 0; }

} // namespace

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : nullptr;
    // Atoms are interned once and live for the process; create them before
    // any baseline is taken.
    (void)atom("a"), (void)atom("b"), (void)atom("c"), (void)atom("next"), (void)atom("leaf"), (void)atom("num"),
        (void)atom("Node");

    if (want(only, "function-owner")) {
        // Releasing a session Function can release its last Program owner
        // while the Value graph worklist is already draining captured cells.
        for (long allow = 0; allow < 4; ++allow) {
            Baseline base;
            auto program = std::make_shared<Program>();
            auto proto = std::make_unique<Proto>();
            proto->constants.push_back(wide(1000));
            program->protos.push_back(std::move(proto));
            auto* function = new FunctionObject();
            function->code = &program->main();
            function->program_owner = std::move(program);
            function->upvalues.push_back(make_cell(wide(200), 5));
            heap_retally(function);
            Value root = Value::adopt(function);
            release_failing(root, base, allow, "function-owner");
        }
    }
    if (want(only, "leaf")) {
        Baseline base;
        Value root = make_string(long_text("leaf", 0));
        release_failing(root, base, 0, "leaf");
    }
    if (want(only, "flat")) {  // dying leaves only: never pushed
        Baseline base;
        Value root = make_array(1000);
        for (std::size_t i = 0; i < 1000; ++i) array_push(root, make_string(long_text("flat", i)));
        release_failing(root, base, 0, "flat");
    }
    if (want(only, "nested")) {  // the original reproducer's abort (first worklist push)
        Baseline base;
        Value root = nested_chain(1000);
        release_failing(root, base, 0, "nested");
    }
    if (want(only, "deep")) {  // 1M levels: no recursion, no allocation
        Baseline base;
        Value root = nested_chain(1000000);
        CHECK(release_failing(root, base, 0, "deep") == 0);
    }
    if (want(only, "deep-reversal")) {  // 3M levels walked by pointer reversal
        Baseline base;
        Value root = make_array(40);
        for (std::size_t i = 0; i < 39; ++i) array_push(root, nested_chain(1));
        array_push(root, nested_chain(3000000));
        CHECK(release_failing(root, base, 0, "deep-reversal") > 0);
    }
    if (want(only, "wide")) {  // spill cannot allocate: pointer reversal
        Baseline base;
        Value root = wide(1000);
        CHECK(release_failing(root, base, 0, "wide") > 0);
    }
    if (want(only, "wide-partial")) {  // the spill grows k times, then fails
        for (long k = 1; k <= 6; ++k) {
            Baseline base;
            Value root = wide(4000);
            release_failing(root, base, k, "wide-partial");
        }
    }
    if (want(only, "spines")) {  // wide parent of deep mixed-kind spines, reversed in place
        Baseline base;
        Value root = make_array(80);
        for (std::size_t i = 0; i < 80; ++i) array_push(root, spine(i < 40 ? 50 : 20000, i));
        CHECK(release_failing(root, base, 0, "spines") > 0);
    }
    if (want(only, "bushy")) {  // fan-out 3, depth 9: resume indexes at every level
        Baseline base;
        Value root = make_array(40);
        for (std::size_t i = 0; i < 40; ++i) array_push(root, bushy(i < 34 ? 1 : 9, 3));
        CHECK(release_failing(root, base, 0, "bushy") > 0);
    }
    if (want(only, "mixed")) {  // shared children and an outside reference survive
        Baseline base;
        Value keep;
        Value shared = make_array(0);
        array_push(shared, make_string(long_text("shared", 0)));
        {
            Value root = make_array(0);
            Value chain = make_array(0);
            for (std::size_t i = 0; i < 2000; ++i) {
                Value o = make_object();
                object_set(o, atom("a"), shared);
                object_set(o, atom("b"), chain);
                object_set(o, atom("c"), make_string(long_text("own", i)));
                chain = o;
                if (i % 7 == 0) array_push(root, chain);
            }
            keep = array_items(root)[array_items(root).size() / 2];
            chain = Value();
            g_allow = 0;
            g_fail = true;
            root = Value();
            g_fail = false;
        }
        std::size_t depth = 0;
        Value cur = keep;
        while (cur.is(Kind::Object)) {
            ++depth;
            CHECK(object_get(cur, atom("a"))->heap() == shared.heap());
            Value next = *object_get(cur, atom("b"));
            cur = next;
        }
        CHECK(depth == 1002);  // root holds nodes 0, 7, ..., 1995; the middle one is node 1001
        CHECK(shared.refcount() == depth + 1);
        cur = Value();
        keep = Value();
        CHECK(shared.refcount() == 1);
        shared = Value();
        CHECK(g_live.load() == base.allocs);
        CHECK(live_heap_bytes() == base.bytes);
    }
    if (want(only, "ctrl")) {  // same graphs with a working allocator
        Baseline base;
        Value a = wide(5000), b = spine(30000, 1), c = nested_chain(100000);
        a = Value(), b = Value(), c = Value();
        CHECK(g_live.load() == base.allocs);
        CHECK(live_heap_bytes() == base.bytes);
    }
    if (want(only, "property")) {
        const std::uint32_t seeds = argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 3000;
        prop::Totals totals;
        for (std::size_t slot = 0; slot < 64; ++slot) (void)prop::key(slot);  // intern before any baseline
        (void)atom("P");
        for (std::uint32_t seed = 1; seed <= seeds; ++seed) prop::run_graph(seed, totals);
        std::printf("property: %ld graphs, %ld releases, %ld injected allocation failures (buffer %d)\n",
                    totals.graphs, totals.releases, totals.injected, NIFT_RT_DESTROY_LOCAL_FOR_TEST);
    }
    return rt_test::finish("value-destroy-oom");
}
