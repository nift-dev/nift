// B2 reference representation (docs/runtime-2/checkpoints/b2-refrep.md): the
// representation exercised through real VM execution. The Phase A guard is
// still on for every product path, so these programs are compiled with the
// non-contractual test switch CompileLimits::reference_probe (no guard, no
// legacy location-alias refusal); the session cases (owner requirement A)
// run through an ordinary VmSession, guard on.
//
// Mutants that must fail it (Makefile test-rt-refrep-mutants):
//   R-B2 (refsem 24) copy-on-write resurrected for arrays (aggregate.cpp)
//   R-B3 (refsem 27) a loop over an expression result iterates live (vm_iter.cpp)
//   R-B3b (refsem 28) a callback native iterates its live receiver (collection_helpers.cpp)
//   R-B4 (refsem 25) copy() returns the same object (copy_graph.cpp)
//   R-B5 (refsem 26) merge mutates its receiver (natives/object_methods.cpp)
//   R-B6 (refsem 29) kImmutable lost on an isolate transfer (isolate_transfer_encode.cpp)
//   R21 (refsem 21)  array/object receivers skip the cycle check (mutation.cpp)
#include "rt/aggregate.h"
#include "rt/compiler.h"
#include "rt/copy_graph.h"
#include "rt/isolate_transfer.h"
#include "rt/mutation.h"
#include "rt/parser.h"
#include "rt/session.h"
#include "rt/session_invocation.h"
#include "rt/source_map.h"
#include "rt/unit_check.h"
#include "rt/vm.h"
#include "rt_test.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace nift::rt;

namespace {

struct Ran {
    std::string reason;  // compile refusal, empty when it ran
    VmResult result;
    std::string output;
};

// Compiles `source` with the reference probe (and, optionally, the script
// invocation bindings `cmd`/`args`) and runs it on the VM.
Ran probe(const std::string& source, const std::vector<std::string>* arguments = nullptr) {
    Ran r;
    auto ast = std::make_unique<Ast>(source);
    const ParseResult parsed = parse_script(*ast);
    if (!parsed.ok) { r.reason = "parse-error"; return r; }
    if (first_opaque_node(*ast, parsed.root) != kNoNode) { r.reason = "opaque"; return r; }
    std::vector<InvocationBinding> invocation;
    CompileLimits limits;
    limits.reference_probe = true;
    if (arguments) {
        invocation = invocation_bindings("cmd", *arguments);
        limits.invocation = &invocation;
    }
    CompileResult compiled = compile_unit(*ast, parsed.root, limits);
    if (!compiled.program) { r.reason = compiled.reason; return r; }
    const SourceMap map = SourceMap::single("t.f", source);
    VmOptions options;
    options.source_map = &map;
    Vm vm(*compiled.program, options);
    r.result = vm.run(r.output);
    return r;
}

void expect(const char* name, const std::string& source, const std::string& want_output,
            const std::string& want_error = "", const std::vector<std::string>* arguments = nullptr) {
    const Ran r = probe(source, arguments);
    const bool stopped = r.result.reason != StopReason::None;
    const bool ok = r.reason.empty() && r.output == want_output &&
                    (want_error.empty() ? !stopped
                                        : stopped && r.result.message.find(want_error) != std::string::npos);
    CHECK(ok);
    if (!ok)
        std::fprintf(stderr, "  case %s: refusal '%s' output [%s] (want [%s]) stop %d '%s' (want '%s')\n", name,
                     r.reason.c_str(), r.output.c_str(), want_output.c_str(), int(r.result.reason),
                     r.result.message.c_str(), want_error.c_str());
}

// Arrays and objects are shared references (R-B2), copy() is a new outer
// object (R-B4), same() is identity, merge builds a new object (R-B5).
void representation() {
    expect("alias-array", "a := [1]\nb := a\nb.push(2)\nprint(a.length())\nprint(same(a, b))\n", "2\ntrue\n");
    expect("alias-object", "o := {\"x\": 1}\np := o\nq := [o]\nr := q[0]\nprint(same(r, p))\n", "true\n");
    expect("argument-shares", "a := [1]\nfn(f(x)) { x.push(2); return 0 }\nf(a)\nprint(a.length())\n", "2\n");
    expect("index-store-shares", "a := [1]\nb := [0]\nb[0] = a\na.push(2)\nprint(b.stringify())\n", "[[1,2]]\n");
    expect("copy-new-outer", "a := [[1]]\nb := copy(a)\nb.push(2)\nprint(a.length())\nprint(same(a, b))\n"
           "x := a[0]\ny := b[0]\nprint(same(x, y))\n", "1\nfalse\ntrue\n");
    expect("deepcopy", "a := [[1]]\nb := deepcopy(a)\nx := a[0]\ny := b[0]\nprint(same(x, y))\n", "false\n");
    expect("same-values", "a := [1]\nb := [1]\nprint(same(a, b))\no := {}\np := {}\nprint(same(o, p))\n",
           "false\nfalse\n");
    expect("merge-new", "o := {\"x\": 1}\np := o.merge({\"y\": 2})\nprint(o.size())\nprint(p.size())\n"
           "print(same(o, p))\nq := o.merge(o)\nprint(q.size())\n", "1\n2\nfalse\n1\n");
    expect("merge-deep-new", "o := {\"x\": {\"a\": 1}}\np := o.merge_deep({\"x\": {\"b\": 2}})\n"
           "print(o.x.size())\nprint(p.x.size())\n", "1\n2\n");
}

// D5 snapshots where copy-on-write gave one (owner ruling, finding 1).
void snapshots() {
    // A loop over an expression's result (R-B3): the body's in-place change of
    // the shared array does not reach the active loop.
    expect("loop-held", "g := [1, 2, 3]\nfn(get()) { return g }\nfor (x : get()) { g.insert(0, 0); print(x) }\n"
           "print(g.length())\n", "1\n2\n3\n6\n");
    // A uniquely held result needs no snapshot (still correct).
    expect("loop-fresh", "fn(make()) { return [1, 2] }\nfor (x : make()) { print(x) }\n", "1\n2\n");
    // Callback natives iterate their entry snapshot (R-B3b).
    // (A callback with a visible effect on its receiver is refused
    // statically, CP22's effect rule; a function called through an alias
    // reaches it.)
    expect("map-snapshot", "g := [1, 2, 3]\nfn(grow(t)) { t.insert(0, 0); return 0 }\nh := g\n"
           "f := (x) => x + grow(h)\nr := g.map(f)\nprint(r.stringify())\nprint(g.length())\n",
           "[1,2,3]\n6\n");
    expect("filter-snapshot", "g := [1, 2, 3]\nfn(shrink(t)) { t.pop(); return true }\nh := g\n"
           "f := (x) => shrink(h)\nr := g.filter(f)\nprint(r.stringify())\nprint(g.length())\n",
           "[1,2,3]\n0\n");
}

// The cycle check reaches array and object receivers (R21): refused
// catchably before anything changes, the receiver intact.
void cycles() {
    expect("push-self", "a := [1]\ntry { a.push(a) } catch (e) { print(e.code) }\nprint(a.length())\n",
           "runtime.reference_cycle\n1\n");
    expect("push-nested", "a := [1]\nb := [a]\ntry { a.push(b) } catch (e) { print(e.code) }\nprint(a.length())\n",
           "runtime.reference_cycle\n1\n");
    expect("set-index", "a := [1]\nb := [a]\ntry { a[0] = b } catch (e) { print(e.code) }\nprint(a[0])\n",
           "runtime.reference_cycle\n1\n");
    expect("insert", "a := [1]\nb := [a]\ntry { a.insert(0, b) } catch (e) { print(e.code) }\n"
           "print(a.length())\n", "runtime.reference_cycle\n1\n");
    expect("splice", "a := [1]\nb := [a]\ntry { a.splice(0, 0, [b]) } catch (e) { print(e.code) }\n"
           "print(a.stringify())\na.splice(0, 0, a)\nprint(a.stringify())\n",
           "runtime.reference_cycle\n[1]\n[1,1]\n");
    // The receiver as its own splice replacement (B2 fix: the insertion never
    // reads a vector's own range; the elements before the removal are
    // inserted, as the copy-on-write register gave; legacy inserts the
    // elements after the removal, [1,1,3,3]: a B5 decision).
    expect("splice-self-middle", "a := [1, 2, 3]\na.splice(1, 1, a)\nprint(a.stringify())\n", "[1,1,2,3,3]\n");
    expect("splice-self-front", "a := [1, 2]\na.splice(0, 0, a)\nprint(a.stringify())\n", "[1,2,1,2]\n");
    expect("splice-self-end", "a := [1, 2]\na.splice(2, 0, a)\nprint(a.stringify())\n", "[1,2,1,2]\n");
    expect("splice-self-full", "a := [1, 2, 3]\na.splice(0, 3, a)\nprint(a.stringify())\n", "[1,2,3]\n");
    expect("splice-self-empty", "a := []\na.splice(0, 0, a)\nprint(a.stringify())\n", "[]\n");
    expect("splice-self-alias", "a := [1, 2]\nb := a\na.splice(1, 0, b)\nprint(b.stringify())\n", "[1,1,2,2]\n");
    expect("splice-self-nested", "a := [1]\nc := [a]\ntry { a.splice(0, 0, c) } catch (e) { print(e.code) }\n"
           "print(a.stringify())\n", "runtime.reference_cycle\n[1]\n");
    expect("through-map", "a := [1]\nm := map()\nm.set(\"k\", a)\ntry { a.push(m) } catch (e) { print(e.code) }\n"
           "print(a.length())\n", "runtime.reference_cycle\n1\n");
    expect("through-cell", "fn(main()) {\n  a := [1]\n  f := () => a.length()\n  b := [f]\n"
           "  try { a.push(b) } catch (e) { print(e.code) }\n  print(a.length())\n  return 0\n}\nmain()\n",
           "runtime.reference_cycle\n1\n");
    expect("acyclic-share", "a := [1]\nb := [a, a]\nc := [b, a]\nprint(c.length())\n", "2\n");
}

// Owner F3: `args` is immutable through every alias (C).
void immutability() {
    const std::vector<std::string> args{"x", "y"};
    expect("args-alias-push", "b := args\nb.push(\"z\")\n", "", "cannot mutate const array", &args);
    expect("args-alias-set", "b := args\nb[0] = \"z\"\n", "", "cannot mutate const array", &args);
    // Validation order (owner F3): immutable before bounds, also for set_index.
    expect("args-set-order", "b := args\nb[9] = \"z\"\n", "", "cannot mutate const array", &args);
    expect("args-pop-order", "b := args\nb.remove(9)\n", "", "cannot mutate const array", &args);
    expect("args-alias-sort", "b := args\nb.reverse()\n", "", "cannot mutate const array", &args);
    expect("args-param", "fn(f(x)) { x.pop(); return 0 }\nf(args)\n", "", "cannot mutate const array", &args);
    expect("args-closure", "b := args\nf := () => { b.insert(0, \"q\"); return 0 }\nf()\n", "",
           "cannot mutate const array", &args);
    expect("args-copy", "b := copy(args)\nb.push(\"z\")\nprint(b.length())\nprint(args.length())\n", "3\n2\n", "",
           &args);
    expect("args-deepcopy", "b := deepcopy(args)\nb.push(\"z\")\nprint(b.length())\n", "3\n", "", &args);
    expect("args-read", "b := args\nprint(b.length())\nfor (x : b) { print(x) }\n", "2\nx\ny\n", "", &args);
}

// Owner F3: kImmutable crosses an isolate boundary (the thread/parallel
// TransferGraph, used by thread(), parallel async, isolate snapshots and
// results; and the cooperative spawn copy), aliasing kept; a mutable array
// stays mutable. R-B6 drops the wire flag.
struct NoProtos : TransferProtoMap {
    bool index_of(const Proto*, std::uint32_t&) const override { return false; }
    const Proto* proto_of(std::uint32_t) const override { return nullptr; }
};
void transfer_immutability() {
    Value args = make_array(2);
    array_push(args, Value::integer(1));
    mutation::make_immutable(args);
    Value plain = make_array(1);
    array_push(plain, args);
    Value root = make_array(3);
    array_push(root, args);
    array_push(root, args);
    array_push(root, plain);
    NoProtos protos;
    TransferGraph graph;
    std::string error;
    Value decoded;
    CHECK(graph.encode(root, protos, error) == TransferStatus::Ok && graph.decode(protos, decoded, error) ==
          TransferStatus::Ok);
    const auto& items = array_view(decoded);
    CHECK(items.size() == 3 && items[0].heap() != args.heap() && items[0].heap() == items[1].heap());
    CHECK(mutation::is_immutable(items[0]) && !mutation::is_immutable(items[2]));
    Value moved = items[0];
    CHECK(mutation::push(moved, Value::integer(2), std::string_view("args")).failure == mutation::Failure::Immutable);
    Value spawned;  // the cooperative spawn copy (vm_coro_ops.cpp)
    CHECK(deep_copy_tuple(&root, 1, &spawned, static_cast<std::size_t>(-1), true) == CopyStatus::Ok);
    CHECK(mutation::is_immutable(array_view(spawned)[0]) && !mutation::is_immutable(array_view(spawned)[2]));
    Value user;  // deepcopy(): mutable
    CHECK(deep_copy(root, user, static_cast<std::size_t>(-1)) == CopyStatus::Ok &&
          !mutation::is_immutable(array_view(user)[0]));
}

// Owner requirement A: a failed mutation never erases or nulls a session
// binding (the take is gone). Guard on: an ordinary VmSession.
void session_atomicity() {
    VmSession session;
    std::string out;
    const auto run = [&](const std::string& source) { return session.execute(source, "<session>", out); };
    const auto length_of = [&](const char* name) -> long {
        const Value v = session.value(name);
        return v.is(Kind::Array) ? static_cast<long>(array_size(v)) : -1;
    };
    CHECK(run("a := [1]").execution.reason == StopReason::None);
    CHECK(run("f := () => a.length()").execution.reason == StopReason::None);  // a captured reference
    CHECK(run("a.pop()").execution.reason == StopReason::None && length_of("a") == 0);
    for (int k = 0; k < 3; ++k) {  // repeated failures
        const auto r = run("a.pop()");
        CHECK(r.execution.reason == StopReason::Error && length_of("a") == 0);
    }
    CHECK(run("a.insert(5, 1)").execution.reason == StopReason::Error && length_of("a") == 0);
    CHECK(run("a.remove(3)").execution.reason == StopReason::Error && length_of("a") == 0);
    CHECK(run("a.splice(\"x\", 1)").execution.reason == StopReason::Error && length_of("a") == 0);
    CHECK(run("a[4] = 1").execution.reason == StopReason::Error && length_of("a") == 0);
    out.clear();
    CHECK(run("print(f())").execution.reason == StopReason::None && out == "0\n");  // the capture sees it
    CHECK(run("a.push(7)").execution.reason == StopReason::None && length_of("a") == 1);  // a later success
    out.clear();
    CHECK(run("print(a.stringify())").execution.reason == StopReason::None && out == "[7]\n");
    out.clear();
    CHECK(run("print(f())").execution.reason == StopReason::None && out == "1\n");
    // Sessions stop cloning: the statement mutated the bound object itself.
    const Value before = session.value("a");
    CHECK(run("a.push(8)").execution.reason == StopReason::None);
    CHECK(session.value("a").heap() == before.heap() && array_size(before) == 2);
}

} // namespace

int main() {
    representation();
    snapshots();
    cycles();
    immutability();
    transfer_immutability();
    session_atomicity();
    return rt_test::finish("refrep-vm");
}
