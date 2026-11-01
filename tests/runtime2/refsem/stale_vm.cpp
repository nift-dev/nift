// D-NEXT-refsem review round 2 (D9): stale-reference controls on the real VM
// path. A container is built and read through the VM (a persistent session),
// beyond the always-relocate size (5,000 elements, each insert followed by a
// VM element read); optionally after a 20,000-element read-after-push churn
// first (which spent the relocation credit while VM reads counted as
// hand-outs). Then a reference is taken into its storage, the container is
// mutated ONCE through the VM, and the reference is used: in a checked build
// (aggregate*.cpp with -DNIFT_RT_CHECKED_REFS under ASan) that use must abort
// with heap-use-after-free.
//
//   stale_vm array|map|set|stack [churn]
#include "rt/aggregate.h"
#include "rt/aggregate_collection.h"
#include "rt/session.h"
#include "rt/string.h"
#include "rt/vm_ops.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace nift::rt;

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string what = argv[1];
    const bool churn = argc > 2 && std::string(argv[2]) == "churn";
    VmSession session;
    std::string output;
    auto run = [&](const std::string& source, const char* name) {
        const auto r = session.execute(source, name, output);
        if (!r.admitted || r.execution.reason != StopReason::None) {
            std::fprintf(stderr, "harness: %s not run (%s %s)\n", name, r.refusal.c_str(), r.execution.message.c_str());
            std::exit(2);
        }
    };
    if (churn) run("big := []\nj := 0\ns := 0\nwhile (j < 20000) { big.push(j); if (big[j] == j) { s = j }; j += 1 }",
                   "churn");
    std::string build, mutate;
    if (what == "array") {
        // A session statement replaces a session-held array (copy-on-write:
        // the session keeps a second reference), so the array is a fresh VM
        // expression's result (no binding holds it), mutated once by
        // set_index, the SetIndex opcode's own implementation, on the only
        // reference (no growth, so only relocation can move the storage).
        auto r = session.evaluate("range(0, 5000).map((v) => \"an element long enough for the heap \" + v)",
                                  "build", output);
        Value array = std::move(r.value);
        r = session.evaluate("0", "drop-last-value", output);
        if (!array.is(Kind::Array) || array.refcount() != 1 || array_size(array) != 5000) {
            std::fprintf(stderr, "harness: the VM-built array is not exclusively held\n");
            return 2;
        }
        const Value& held = array_items(array)[0];
        Fault fault;  // one element store, set_index (the SetIndex opcode's own implementation): no growth
        if (!set_index(nullptr, array, Value::integer(1), Value::integer(-1), fault)) return 2;
        const Value copy = held;  // stale: must abort with heap-use-after-free in a checked build
        std::printf("%d\n", static_cast<int>(copy.kind()));
        return 0;
    } else if (what == "map") {
        build = "a := map()\na.set(0, \"a value long enough for the heap\")\ni := 1\n"
                "x := 0\nwhile (i < 5000) { a.set(i, \"v\"); if (a.get(0) == \"v\") { x = i }; i += 1 }";
        mutate = "a.set(-1, 0)";
    } else if (what == "set") {
        build = "a := set()\na.add(\"a member long enough for the heap\")\ni := 1\n"
                "x := 0\nwhile (i < 5000) { a.add(i); if (a.contains(0)) { x = i }; i += 1 }";
        mutate = "a.add(-1)";
    } else if (what == "stack") {
        build = "a := stack()\na.push(\"an element long enough for the heap\")\ni := 1\n"
                "x := 0\nwhile (i < 5000) { a.push(\"v\"); if (a.top() == \"v\") { x = i }; i += 1 }";
        mutate = "a.push(-1)";
    } else {
        return 2;
    }
    run(build, "build");
    Value v = session.value("a");
    const Value& held = collection_items(v)[what == "map" ? 1 : 0];
    if (!held.is(Kind::String)) {
        std::fprintf(stderr, "harness: element is not the string (kind %d)\n", static_cast<int>(held.kind()));
        return 2;
    }
    std::fprintf(stderr, "before: %zu\n", string_view_of(held).size());
    const HeapObject* object = v.heap();
    v = Value();  // the collection is mutated in place (a reference) by the VM statement
    run(mutate, "mutate-once");
    if (getenv("STALE_DEBUG")) {
        Value after = session.value("a");
        std::fprintf(stderr, "same object: %d refcount %u\n", after.heap() == object, after.heap()->refcount);
    }
    // The use: read in this (instrumented) translation unit, so ASan checks it.
    const Value copy = held;  // stale: must abort with heap-use-after-free in a checked build
    std::printf("%d\n", static_cast<int>(copy.kind()));
    return 0;
}
