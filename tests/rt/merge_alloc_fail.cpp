// Owner 2026-11-10 08:57 (combined-b3-b5.md section 65): merge and merge_deep build their result
// with the one-step clone (build::Object::shallow_clone) instead of one set per entry. This test
// injects an allocation failure at every allocation point of a call, for the production natives
// (clone) and for a reference copy of the former per-entry construction, and checks after each
// failure: no leak (outstanding allocations and the live-heap tally back to their values before
// the call), no partial construction visible (the receiver and the argument unchanged: same
// entries, same values, same nested refcounts), and, once the call succeeds, the contract:
//   - a fresh outer object (never the receiver or the argument);
//   - merge: nested values shared (B2), the argument's entries win, the order kept;
//   - merge_deep: nested objects merged into new objects, the sources unchanged;
//   - the live-heap tally equals the result's footprint (accounting).
#include "rt_test.h"
#include "builtins_support.h"

#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/heap_account.h"
#include "rt/natives/object_methods.h"
#include "rt/natives/support.h"
#include "storage_access.h"  // tooling 2: the construction shims (tests only)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {
long g_countdown = -1;  // fail the allocation when it reaches 0; -1: never
long g_live = 0;
constexpr std::size_t kHeader = 16;
} // namespace

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void* operator new(std::size_t n) {
    if (g_countdown >= 0 && g_countdown-- == 0) throw std::bad_alloc();
    auto* p = static_cast<unsigned char*>(std::malloc(n + kHeader));
    if (!p) throw std::bad_alloc();
    std::memcpy(p, &n, sizeof n);
    ++g_live;
    return p + kHeader;
}
void operator delete(void* q) noexcept {
    if (!q) return;
    --g_live;
    std::free(static_cast<unsigned char*>(q) - kHeader);
}
void operator delete(void* q, std::size_t) noexcept { operator delete(q); }

using namespace nift::rt;
using namespace nift::rt::natives;

namespace {

using Native = Status (*)(const Context&, const Value*, std::uint32_t, Value&, std::string&);

// The former construction (before the clone), kept here as the reference: one set per entry.
build::Object per_entry(const Value& source) {
    build::Object out;
    for (const auto& entry : object_view(source)) out.set(entry.first, entry.second);
    return out;
}
Status old_merge(const Context&, const Value* args, std::uint32_t, Value& out, std::string&) {
    build::Object result = per_entry(args[0]);
    for (const auto& entry : object_entries(args[1])) result.set(entry.first, entry.second);
    out = std::move(result).publish();
    return Status::Ok;
}

struct Snapshot {
    std::vector<std::pair<Atom, std::uint64_t>> entries;  // key, identity or scalar
    std::vector<std::uint32_t> refcounts;
};
std::uint64_t ident(const Value& v) {
    return v.is_heap() ? reinterpret_cast<std::uintptr_t>(v.heap()) : static_cast<std::uint64_t>(v.as_int());
}
Snapshot snap(const Value& object) {
    Snapshot s;
    for (const auto& e : object_view(object)) {
        s.entries.push_back({e.first, ident(e.second)});
        s.refcounts.push_back(e.second.refcount());
    }
    return s;
}
bool same(const Snapshot& a, const Snapshot& b) { return a.entries == b.entries && a.refcounts == b.refcounts; }

Value make_receiver(std::size_t n, const Value& shared_nested) {
    Value o = make_object();
    for (std::size_t i = 0; i < n; ++i) object_set(o, atom("k" + std::to_string(i)), Value::integer(static_cast<std::int64_t>(i)));
    object_set(o, atom("nested"), shared_nested);
    return o;
}

// Fails every allocation point of `fn(receiver, argument)` in turn; returns the number of points.
long sweep(const char* label, Native fn, const Value& receiver, const Value& argument) {
    const Snapshot r0 = snap(receiver), a0 = snap(argument);
    const long live0 = g_live;
    const std::size_t heap0 = live_heap_bytes();
    long points = 0;
    for (long k = 0;; ++k) {
        Value args[2] = {receiver, argument};
        Value out;
        std::string error;
        Context context;
        bool threw = false;
        g_countdown = k;
        try {
            fn(context, args, 2, out, error);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        g_countdown = -1;
        if (!threw) {
            CHECK(out.is(Kind::Object) && out.heap() != receiver.heap() && out.heap() != argument.heap());
            // accounting: the tally grew by exactly the result's own footprint
            std::vector<const Value*> roots = {&receiver, &argument, &out};
            CHECK(live_heap_bytes() - heap0 + builtins_test::footprint({&receiver, &argument}) ==
                  builtins_test::footprint(roots));
            break;
        }
        ++points;
        args[0] = Value(); args[1] = Value();
        CHECK(g_live == live0);               // no leak
        CHECK(live_heap_bytes() == heap0);    // tally unchanged
        CHECK(same(snap(receiver), r0));      // no partial construction visible
        CHECK(same(snap(argument), a0));
    }
    std::printf("%s: %ld allocation point(s), each failed without a leak or a visible change\n", label, points);
    return points;
}

} // namespace

int main() {
    Value nested = make_object();
    object_set(nested, atom("x"), Value::integer(1));
    const Value receiver = make_receiver(200, nested);
    Value argument = make_object();
    object_set(argument, atom("k3"), Value::integer(-3));
    object_set(argument, atom("new"), Value::integer(7));
    Value deep_arg = make_object();
    Value deep_inner = make_object();
    object_set(deep_inner, atom("y"), Value::integer(2));
    object_set(deep_arg, atom("nested"), deep_inner);

    const long clone_points = sweep("merge (clone)", obj_merge, receiver, argument);
    const long old_points = sweep("merge (former per-entry construction)", old_merge, receiver, argument);
    sweep("merge_deep (clone)", obj_merge_deep, receiver, deep_arg);
    sweep("merge (receiver is the argument)", obj_merge, receiver, receiver);
    CHECK(clone_points > 0 && old_points > 0);
    std::printf("allocation points: clone %ld, former per-entry %ld\n", clone_points, old_points);

    // The contract on success, against the former construction.
    {
        Value args[2] = {receiver, argument};
        Value a, b;
        std::string error;
        Context context;
        CHECK(obj_merge(context, args, 2, a, error) == Status::Ok);
        CHECK(old_merge(context, args, 2, b, error) == Status::Ok);
        CHECK(object_size(a) == object_size(b) && object_size(a) == 202);
        const auto& ea = object_view(a);
        const auto& eb = object_view(b);
        for (std::size_t i = 0; i < ea.size() && i < eb.size(); ++i)
            CHECK(ea[i].first == eb[i].first && ident(ea[i].second) == ident(eb[i].second));
        CHECK(object_get(a, atom("k3"))->as_int() == -3 && object_get(receiver, atom("k3"))->as_int() == 3);
        CHECK(object_get(a, atom("nested"))->heap() == nested.heap());  // merge: nested shared (B2)
        Value deep;
        Value dargs[2] = {receiver, deep_arg};
        CHECK(obj_merge_deep(context, dargs, 2, deep, error) == Status::Ok);
        const Value* merged = object_get(deep, atom("nested"));
        CHECK(merged && merged->heap() != nested.heap() && merged->heap() != deep_inner.heap());  // new object
        CHECK(object_get(*merged, atom("x"))->as_int() == 1 && object_get(*merged, atom("y"))->as_int() == 2);
        CHECK(object_size(nested) == 1 && object_size(deep_inner) == 1);  // sources unchanged
    }
    return rt_test::finish("merge-alloc-fail");
}
