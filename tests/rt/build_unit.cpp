// Tooling checkpoint (component 2, docs/runtime-2/checkpoints/tooling.md 2.5):
// the builder contract (rt/build.h).
//   construction  a builder populates a new value no other code can reach;
//   publication   publish() && moves the value out, after which the builder
//                 holds nothing, so no use of it (before or after any number of
//                 moves, through any alias) can reach the published value;
//   failure       an allocation failure during construction leaks nothing and
//                 no partial value escapes; publication itself never allocates.
// A use of a spent builder runs in a forked child whose SIGSEGV/SIGABRT handler
// (or normal path) reports whether the published value changed, so a mutant
// that keeps storage authority (B-M1, B-M2) is caught by observing the change,
// not by an abort. Built three ways (make test-rt-build): release, checked
// (NIFT_RT_CHECKED_REFS: the spent check aborts) and each mutant.
#include "rt/aggregate.h"
#include "rt/copy_graph.h"
#include "rt/heap_account.h"
#include "rt/heap.h"
#include "rt/compare.h"
#include "rt/string.h"
#include "storage_access.h"  // the construction shims: the primitives behind builders and keys

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

using namespace nift::rt;

static long remaining = -1;
static long outstanding = 0;
void* operator new(std::size_t bytes) {
    if (remaining == 0) { remaining = -1; throw std::bad_alloc(); }
    if (remaining > 0) --remaining;
    if (void* p = std::malloc(bytes ? bytes : 1)) { ++outstanding; return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { if (p) --outstanding; std::free(p); }
void operator delete[](void* p) noexcept { if (p) --outstanding; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (p) --outstanding; std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (p) --outstanding; std::free(p); }

static int checks = 0, failures = 0;
static std::size_t live() { return live_heap_bytes() - heap_charged_bytes(); }
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)

#if defined(NIFT_RT_CHECKED_REFS)
static const bool kChecked = true;
#else
static const bool kChecked = false;
#endif

// ---- reuse of a spent builder, in a child process ------------------------
// The child records the published value's observable state before the reuse,
// runs the reuse, and reports on every exit path (return, SIGSEGV, SIGABRT)
// whether that state changed: 40 unchanged and the reuse returned (only a
// mutant can return from it), 41 unchanged and the reuse faulted or aborted
// (the builder held nothing), 50 changed (storage authority was retained).
static const Value* g_published = nullptr;
static std::size_t g_size_before = 0;
static long g_refs_before = 0;
static std::size_t observed_size() {
    const Value& v = *g_published;
    if (v.is(Kind::Array)) return array_size(v);
    if (v.is(Kind::Object)) return object_size(v);
    if (v.is(Kind::Struct)) return struct_fields(v).size();
    return 0;
}
// The contents too (an element replaced in place keeps the size).
static std::vector<Value>* g_snapshot = nullptr;
static std::vector<Value> contents(const Value& v) {
    std::vector<Value> out;
    if (v.is(Kind::Array)) for (const Value& item : array_view(v)) out.push_back(item);
    if (v.is(Kind::Object)) for (const auto& entry : object_view(v)) { out.push_back(Value::integer(entry.first.id)); out.push_back(entry.second); }
    if (v.is(Kind::Struct)) for (const auto& field : struct_fields(v)) { out.push_back(Value::integer(field.first.id)); out.push_back(field.second); }
    return out;
}
static bool same_contents() {
    const std::vector<Value> now = contents(*g_published);
    if (now.size() != g_snapshot->size()) return false;
    for (std::size_t i = 0; i < now.size(); ++i)
        if (now[i].kind() != (*g_snapshot)[i].kind() || !equal(now[i], (*g_snapshot)[i])) return false;
    return true;
}
static bool unchanged() {
    return observed_size() == g_size_before && g_published->refcount() == g_refs_before && same_contents();
}
extern "C" void on_fault(int) { _exit(unchanged() ? 41 : 50); }

template <class Reuse>
static int reuse_in_child(const Value& published, Reuse reuse) {
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        g_published = &published;
        g_size_before = observed_size();
        g_refs_before = published.refcount();
        static std::vector<Value> snapshot;
        snapshot = contents(published);
        g_snapshot = &snapshot;
        g_refs_before = published.refcount();  // after the snapshot (it holds the elements, not the value)
        std::signal(SIGSEGV, on_fault);
        std::signal(SIGBUS, on_fault);
        std::signal(SIGABRT, on_fault);
        reuse();
        _exit(unchanged() ? 40 : 50);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
// A spent builder must leave the published value unchanged; in checked builds
// it must also abort (41), in release builds it faults on its own null value (41).
static void expect_harmless_reuse(const char* what, int code) {
    if (code == 41) return void(++checks);
    ++checks; ++failures;
    std::fprintf(stderr, "FAIL reuse %s: child exit %d (%s)\n", what, code,
                 code == 50 ? "the published value changed: the builder kept storage authority"
                 : code == 40 ? "the reuse returned without touching anything" : "unexpected");
}

// ---- publication -----------------------------------------------------------
static void publication() {
    {   // Array: publish, then reuse (push, set, size, publish again)
        build::Array b(2);
        b.push(Value::integer(1));
        b.push(Value::integer(2));
        Value v = std::move(b).publish();
        CHECK(v.is(Kind::Array) && array_size(v) == 2);
        CHECK(v.refcount() == 1);  // the builder holds no reference
        expect_harmless_reuse("Array::push", reuse_in_child(v, [&] { b.push(Value::integer(3)); }));
        expect_harmless_reuse("Array::set", reuse_in_child(v, [&] { b.set(0, Value::integer(9)); }));
        expect_harmless_reuse("Array::publish twice", reuse_in_child(v, [&] {
            Value again = std::move(b).publish();  // a mutant hands out another reference here
            std::abort();  // report through the handler while `again` is alive
        }));
    }
    {   // ArrayBulk (the natives' form): publish, then append / tally
        build::ArrayBulk b(2);
        b.append(Value::integer(1));
        b.tally();
        Value v = std::move(b).publish();
        CHECK(array_size(v) == 1 && v.refcount() == 1);
        expect_harmless_reuse("ArrayBulk::append", reuse_in_child(v, [&] { b.append(Value::integer(2)); }));
    }
    {   // Object
        build::Object b;
        b.set(atom("a"), Value::integer(1));
        Value v = std::move(b).publish();
        CHECK(object_size(v) == 1 && v.refcount() == 1);
        expect_harmless_reuse("Object::set", reuse_in_child(v, [&] { b.set(atom("b"), Value::integer(2)); }));
    }
    {   // Struct
        build::Struct b(atom("Point"), 2);
        b.add(atom("x"), Value::integer(1));
        Value v = std::move(b).publish();
        CHECK(struct_fields(v).size() == 1 && v.refcount() == 1);
        expect_harmless_reuse("Struct::add", reuse_in_child(v, [&] { b.add(atom("y"), Value::integer(2)); }));
    }
}

// ---- repeated moves and aliases ---------------------------------------------
static void moves_and_aliases() {
    build::Array a(4);
    a.push(Value::integer(1));
    build::Array b(std::move(a));
    b.push(Value::integer(2));
    build::Array c(std::move(b));
    c.push(Value::integer(3));
    build::Array& alias = c;      // a reference to the builder
    build::Array* pointer = &c;   // and a pointer
    Value v = std::move(alias).publish();
    CHECK(array_size(v) == 3 && v.refcount() == 1);
    expect_harmless_reuse("moved-from (first)", reuse_in_child(v, [&] { a.push(Value::integer(7)); }));
    expect_harmless_reuse("moved-from (second)", reuse_in_child(v, [&] { b.push(Value::integer(7)); }));
    expect_harmless_reuse("alias after publish", reuse_in_child(v, [&] { pointer->push(Value::integer(7)); }));
    build::ArrayBulk x(2);
    x.append(Value::integer(1));
    build::ArrayBulk y(std::move(x));
    Value w = std::move(y).publish();
    CHECK(array_size(w) == 1 && w.refcount() == 1);
    expect_harmless_reuse("ArrayBulk moved-from", reuse_in_child(w, [&] { x.append(Value::integer(5)); }));
    build::Object o;
    o.set(atom("k"), Value::integer(1));
    build::Object p(std::move(o));
    Value ov = std::move(p).publish();
    CHECK(object_size(ov) == 1 && ov.refcount() == 1);
    expect_harmless_reuse("Object moved-from", reuse_in_child(ov, [&] { o.set(atom("z"), Value::integer(5)); }));
}

// ---- allocation failure: during construction, at publication ----------------
static Value nested(bool& threw, int k) {
    // An object holding an array and a struct, each built by its own builder,
    // published into the parent: nested builders.
    remaining = k;
    try {
        build::Object top;
        build::Array items(3);
        items.push(make_string(std::string(40, 'a')));
        items.push(make_string(std::string(40, 'b')));
        build::Struct point(atom("P"), 2);
        point.add(atom("x"), make_string(std::string(40, 'x')));
        top.set(atom("items"), std::move(items).publish());
        top.set(atom("point"), std::move(point).publish());
        build::ArrayBulk bulk(2);
        bulk.append(make_string(std::string(40, 'c')));
        bulk.tally();
        top.set(atom("bulk"), std::move(bulk).publish());
        Value out = std::move(top).publish();
        remaining = -1;
        threw = false;
        return out;
    } catch (const std::bad_alloc&) {
        remaining = -1;
        threw = true;
        return Value();
    }
}
static void allocation_failure() {
    for (const char* name : {"items", "point", "bulk", "x", "P", "k", "S", "f"}) (void)atom(name);  // interned once
    const long before = outstanding;
    const std::size_t live_before = live();
    int sites = 0;
    for (int k = 0;; ++k) {
        bool threw = false;
        Value out = nested(threw, k);
        if (!threw) {
            CHECK(out.is(Kind::Object) && object_size(out) == 3 && out.refcount() == 1);
            break;
        }
        ++sites;
        CHECK(out.is(Kind::Null));  // no partial value escapes
        CHECK(outstanding == before);  // the unpublished builders freed everything
        CHECK(live() == live_before);
    }
    CHECK(sites >= 8);
    std::printf("allocation failure: %d failing sites in nested construction, none leaks\n", sites);
    // publication allocates nothing: the next allocation would fail
    build::Array a(1);
    a.push(Value::integer(1));
    build::ArrayBulk bulk(1);
    bulk.append(Value::integer(1));
    build::Object o;
    o.set(atom("k"), Value::integer(1));
    build::Struct s(atom("S"), 1);
    s.add(atom("f"), Value::integer(1));
    remaining = 0;
    bool threw = false;
    try {
        Value va = std::move(a).publish();
        bulk.tally();
        Value vb = std::move(bulk).publish();
        Value vo = std::move(o).publish();
        Value vs = std::move(s).publish();
        CHECK(array_size(va) == 1 && array_size(vb) == 1 && object_size(vo) == 1 && struct_fields(vs).size() == 1);
    } catch (const std::bad_alloc&) { threw = true; }
    remaining = -1;
    CHECK(!threw);
    CHECK(outstanding == before);
}

// ---- graph construction: clone_constant (builders) and deep_copy (the key) ----
static void graph_construction() {
    build::Object inner;
    inner.set(atom("c"), make_string(std::string(40, 'c')));
    Value shared = std::move(inner).publish();
    build::Array arr(2);
    arr.push(shared);
    arr.push(shared);  // a DAG: one node reached twice
    build::Object root;
    root.set(atom("a"), std::move(arr).publish());
    root.set(atom("b"), shared);
    Value source = std::move(root).publish();
    const long before = outstanding;
    const std::size_t live_before = live();
    for (int which = 0; which < 2; ++which) {
        int sites = 0;
        for (int k = 0;; ++k) {
            Value out;
            remaining = k;
            bool threw = false;
            try {
                CHECK((which == 0 ? clone_constant(source, out, static_cast<std::size_t>(-1))
                                  : deep_copy(source, out, static_cast<std::size_t>(-1))) == CopyStatus::Ok);
            } catch (const std::bad_alloc&) { threw = true; }
            remaining = -1;
            if (!threw) {
                CHECK(out.is(Kind::Object) && out.refcount() == 1);
                CHECK(object_size(source) == 2 && shared.refcount() == 4);  // the source is unchanged
                const Value& a = *object_get(out, atom("a"));
                CHECK(a.heap() != object_get(source, atom("a"))->heap());  // fresh outer nodes
                if (which == 1) CHECK(array_items(a)[0].heap() == array_items(a)[1].heap());  // DAG kept
                break;
            }
            ++sites;
            CHECK(out.is(Kind::Null));
            CHECK(outstanding == before && live() == live_before);
        }
        std::printf("%s: %d failing allocation sites, none leaks, the source unchanged\n",
                    which == 0 ? "clone_constant" : "deep_copy", sites);
    }
}

// ---- the run-time layer of both tiers (tooling.md 2.2, 2.4) -----------------
// The in-place sites and the builders share the primitives; in checked builds
// check_fresh refuses a published receiver (shared or immutable) by aborting
// before any write. Release builds do not check: there the in-place sites rest
// on their owners (compile and link time) and on the suites of 2.4.
static void in_place_runtime_layer() {
    if (!kChecked) return;
    build::Array b(2);
    b.push(Value::integer(1));
    Value published = std::move(b).publish();
    Value alias = published;  // a second holder: published, shared
    expect_harmless_reuse("primitive on a shared receiver",
                          reuse_in_child(published, [&] { array_push(published, Value::integer(2)); }));
    build::Object o;
    o.set(atom("k"), Value::integer(1));
    Value frozen = std::move(o).publish();
    mark_immutable(frozen.heap());  // the flag the protocol sets (rt/heap.h)
    expect_harmless_reuse("primitive on an immutable receiver",
                          reuse_in_child(frozen, [&] { object_set(frozen, atom("j"), Value::integer(2)); }));
}

int main() {
    in_place_runtime_layer();
    publication();
    moves_and_aliases();
    allocation_failure();
    graph_construction();
    std::printf("rt build unit (%s): %d checks, %d failures\n", kChecked ? "checked" : "release", checks, failures);
    return failures ? 1 : 0;
}
