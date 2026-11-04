// B2 reference representation (docs/runtime-2/checkpoints/b2-refrep.md): the
// representation exercised through real VM execution. The Phase A guard is
// still on for every product path, so this binary is the test configuration:
// it links compiler objects built with -DNIFT_RT_REFERENCE_PROBE
// (rt/reference_probe.h: no guard, no legacy location-alias refusal), a
// compile-time configuration that no production build contains. Production
// behaviour (guard on) is covered by the ordinary suites and refrep_cli.py.
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
#include "rt/heap.h"
#include "rt/compiler.h"
#include "rt/copy_graph.h"
#include "rt/isolate_transfer.h"
#include "rt/mutation.h"
#include "rt/parallel_task.h"
#include "rt/parser.h"
#include "rt/reference_probe.h"
#include "rt/session.h"
#include "rt/session_invocation.h"
#include "rt/source_map.h"
#include "rt/unit_check.h"
#include "rt/vm.h"
#include "storage_access.h"  // tooling 2: the construction shims (tests only)
#include "rt_test.h"

#include <cstdio>
#include <stdexcept>
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
    limits.cooperative_threads = true;  // thread() as a CP22 cooperative task (identity tests)
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
// binding (the take is gone). A VmSession of this binary: the probe configuration
// (production: refrep_cli.py through real `nift -i`).
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


// Owner rulings 5 and 6 (b2-refrep.md, "identity"): same() is identity for
// arrays and objects (an alias is the same, a distinct equal value is not,
// a mutation through an alias keeps identity, copy/deepcopy are distinct),
// through function arguments, closures and captures, nested mutation, thread
// and async transfers (one alias-preserving copy: distinct from the source,
// aliasing kept inside) and a persistent session.
void identity() {
    expect("id-array", "a := [1]\nb := a\nc := [1]\nprint(same(a, b))\nprint(same(a, c))\nb.push(2)\n"
           "print(same(a, b))\nprint(a.length())\nd := copy(a)\ne := deepcopy(a)\nprint(same(a, d))\n"
           "print(same(a, e))\n", "true\nfalse\ntrue\n2\nfalse\nfalse\n");
    // Objects: the VM stores elements of arrays only (plain objects have no
    // script-level store; object_put is covered at the representation layer),
    // so a mutation through an object alias is shown through a member array.
    expect("id-object", "o := {\"x\": [1]}\np := o\nq := {\"x\": [1]}\nprint(same(o, p))\nprint(same(o, q))\n"
           "c := p.x\nc.push(2)\nprint(same(o, p))\nprint(o.x.length())\nd := copy(o)\ne := deepcopy(o)\n"
           "print(same(o, d))\nprint(same(o, e))\n", "true\nfalse\ntrue\n2\nfalse\nfalse\n");
    expect("id-argument", "fn(id(x)) { return x }\na := [1]\no := {}\nb := id(a)\np := id(o)\nprint(same(b, a))\n"
           "print(same(p, o))\n", "true\ntrue\n");
    expect("id-closure", "fn(main()) {\n  a := [1]\n  o := {\"k\": [1]}\n  f := () => a\n  g := () => o\n"
           "  h := () => { a.push(2)\n c := o.k\n c.push(2)\n return 0 }\n  h()\n  x := f()\n  y := g()\n"
           "  print(same(x, a))\n  print(same(y, o))\n  print(a.length())\n  print(o.k.length())\n  return 0\n}\n"
           "main()\n", "true\ntrue\n2\n2\n");
    expect("nested-mutation", "a := [[1]]\nb := a[0]\nb.push(2)\nprint(a.stringify())\no := {\"x\": [1]}\nc := o.x\n"
           "c.push(2)\np := o\nprint(p.x.length())\nr := [{\"k\": [1]}]\ne := r[0]\nd := e.k\nd.push(5)\n"
           "print(r[0].k.length())\nf := r[0]\nprint(same(e, f))\n", "[[1,2]]\n2\n2\ntrue\n");
    expect("id-thread", "fn(w(x, y)) {\n  x.push(2)\n  return y.length()\n}\nfn(r(x)) { return x }\na := [1]\n"
           "t := thread(w, a, a)\nprint(t.join())\nprint(a.length())\nu := thread(r, a)\nb := u.join()\n"
           "print(same(a, b))\nprint(b.length())\no := {\"k\": 1}\nv := thread(r, o)\nc := v.join()\n"
           "print(same(o, c))\n", "2\n1\nfalse\n1\nfalse\n");
    // Review H1 (case table; probe rows: production refuses these aliases, `lent`/`aliasing`):
    // a literal is a description, every evaluation a new graph, nested aggregates included.
    expect("literal-nested-array", "fn(f()) { return [[1], [2]] }\nx := f()\ne := x[0]\ne.push(9)\ny := f()\n"
           "print(y.stringify())\nprint(same(x, y))\n", "[[1],[2]]\nfalse\n");
    expect("literal-nested-json", "fn(f()) { return {\"a\": [1], \"b\": {\"c\": [2]}} }\nx := f()\nn := x.a\n"
           "n.push(9)\nm := x.b\nq := m.c\nq.push(8)\ny := f()\nprint(y.stringify())\nya := y.a\n"
           "print(same(n, ya))\n", "{\"a\":[1],\"b\":{\"c\":[2]}}\nfalse\n");
    // Re-review C3 (copy/deepcopy row): copy(x) is a new outer object sharing
    // x's contents, which are this evaluation's own: a push through the copy is
    // never seen by a later evaluation (with a shared constant it is).
    expect("literal-copy", "fn(f()) { return {\"k\": [1]} }\nx := f()\nc := copy(x)\nck := c.k\nck.push(2)\n"
           "d := deepcopy(x)\ndk := d.k\ndk.push(3)\ny := f()\nprint(x.k.length())\nprint(y.k.length())\n"
           "print(same(c, x))\nprint(same(d, x))\n", "2\n1\nfalse\nfalse\n");
    expect("literal-alias", "fn(f()) { return {\"a\": [1]} }\nx := f()\nz := x\nn := z.a\nn.push(2)\n"
           "print(same(x, z))\nprint(x.a.length())\n", "true\n2\n");
    expect("id-async", "fn[async](w(x)) { x.push(9); return x.length() }\nfn(main()) {\n  a := [1]\n"
           "  f := w(a)\n  a.push(2)\n  print(await f)\n  print(a.length())\n  return 0\n}\nmain()\n", "2\n2\n");
    // Session persistence (this binary's VmSession compiles with the probe).
    VmSession session;
    std::string out;
    const auto run = [&](const std::string& source) {
        out.clear();
        const SessionResult r = session.execute(source, "<session>", out);
        const bool ok = r.admitted && r.execution.reason == StopReason::None;
        if (!ok) std::fprintf(stderr, "  session [%s]: refusal '%s' stop %d '%s'\n", source.c_str(),
                              r.refusal.c_str(), int(r.execution.reason), r.execution.message.c_str());
        return ok;
    };
    CHECK(run("a := [1]") && run("b := a") && run("b.push(2)"));
    CHECK(session.value("a").heap() == session.value("b").heap() && array_size(session.value("a")) == 2);
    const auto member = [&](const char* name) -> long {
        const Value v = session.value(name);
        const Value* x = v.is(Kind::Object) ? object_get(v, atom("x")) : nullptr;
        return x && x->is(Kind::Array) ? static_cast<long>(array_size(*x)) : -1;
    };
    CHECK(run("o := {\"x\": [1]}") && run("p := o") && run("d := p.x") && run("d.push(2)"));
    CHECK(session.value("o").heap() == session.value("p").heap() && member("o") == 2);
    // (sessions refuse same(), builtin-lazy-args: identity is compared directly)
    CHECK(session.value("a").heap() == session.value("b").heap());
    CHECK(run("c := copy(a)") && session.value("c").heap() != session.value("a").heap());
    CHECK(run("g := () => { a.push(3)\n e := o.x\n e.push(3)\n return 0 }") && run("g()"));
    CHECK(array_size(session.value("b")) == 3 && member("p") == 3);
    CHECK(run("try { a.push(b) } catch (e) { print(e.code) }") && out == "runtime.reference_cycle\n");
    CHECK(array_size(session.value("a")) == 3 && session.value("a").heap() == session.value("b").heap());
}

// Owner requirement E (b2-refrep.md, "sort protection"): while a comparator
// sort runs, its receiver is protected (kSortProtected, the Vm registry) and
// any mutation of it through any holder is the catchable
// runtime.sort_receiver_mutated; a nested sort of another array works. Every
// exit path is checked DIRECTLY, on the registry and the receiver's header,
// at the moment the exit completes: through the VM's synchronous output
// flush (flush_threshold 0), at a print placed right after the exit, and
// after the run for the exits that end it. Mutants S1-S5 must fail here.
struct Watch {
    const Vm* vm = nullptr;
    const VmSession* session = nullptr;
    Value receiver;                 // captured from the registry inside the comparator
    std::vector<std::string> lines;
    std::vector<std::size_t> protections;  // the registry size at each line
    std::vector<int> protected_bit;        // the receiver's bit at each line (-1: none captured)
    std::vector<std::size_t> live;         // process-wide live protections at each line (every VM)
    const char* throw_at = nullptr;        // a line that throws from the flush (an exception exit)
};
bool sort_bit(const Value& v) { return v.is_heap() && is_sort_protected(v.heap()); }
void on_flush(std::string& output, void* context) {
    auto& w = *static_cast<Watch*>(context);
    std::size_t start = 0;
    while (start < output.size()) {
        const std::size_t end = output.find('\n', start);
        const std::string line = output.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? output.size() : end + 1;
        const std::size_t count = w.vm ? w.vm->sort_protections() : w.session->sort_protections();
        if (w.vm && w.receiver.is_null() && count > 0) w.receiver = *w.vm->sort_protected(0);
        w.lines.push_back(line);
        w.protections.push_back(count);
        w.protected_bit.push_back(w.receiver.is_null() ? -1 : int(sort_bit(w.receiver)));
        w.live.push_back(Vm::live_sort_protections());
        if (w.throw_at && line == w.throw_at) { output.clear(); throw std::runtime_error("flush exit"); }
    }
    output.clear();
}
// Runs `source` with the probe and the watch; the receiver captured at the
// first line printed while a sort is protected.
VmResult watched(const std::string& source, Watch& w, bool* threw = nullptr, bool parallel = false) {
    auto ast = std::make_unique<Ast>(source);
    const ParseResult parsed = parse_script(*ast);
    CHECK(parsed.ok);
    CompileLimits limits;
    limits.parallel_tasks = parallel;  // the production default model: tasks in their own isolate VM
    CompileResult compiled = compile_unit(*ast, parsed.root, limits);
    CHECK(compiled.program != nullptr);
    if (!compiled.program) { std::fprintf(stderr, "  refused: %s\n", compiled.reason.c_str()); return {}; }
    const SourceMap map = SourceMap::single("t.f", source);
    VmOptions options;
    options.source_map = &map;
    options.flush_threshold = 0;
    options.flush = on_flush;
    options.flush_context = &w;
    if (parallel) options.parallel = std::make_shared<ParallelRuntime>(1);
    VmResult result;
    {
        Vm vm(*compiled.program, options);
        w.vm = &vm;
        std::string out;
        try {
            result = vm.run(out);
            on_flush(out, &w);
        } catch (const std::runtime_error&) {
            if (threw) *threw = true;
        }
        // After the run (every exit): nothing protected, the receiver released.
        CHECK(vm.sort_protections() == 0);
        w.vm = nullptr;
    }
    return result;
}
// The line `name` was printed with the registry at `count` and the
// receiver's bit at `bit`.
bool line_state(const Watch& w, const std::string& name, std::size_t count, int bit) {
    for (std::size_t k = 0; k < w.lines.size(); ++k)
        if (w.lines[k] == name) return w.protections[k] == count && w.protected_bit[k] == bit;
    return false;
}
void report(const char* name, const Watch& w, bool ok) {
    CHECK(ok);
    if (ok) return;
    std::fprintf(stderr, "  case %s:", name);
    for (std::size_t k = 0; k < w.lines.size(); ++k)
        std::fprintf(stderr, " [%s reg=%zu bit=%d]", w.lines[k].c_str(), w.protections[k], w.protected_bit[k]);
    std::fprintf(stderr, "\n");
}

void sort_protection() {
    {   // Completion: protected inside the comparator, released when the sort returns.
        Watch w;
        const VmResult r = watched("a := [3, 1, 2]\na.sort((x, y) => { print(\"in\")\n return x < y })\n"
                                   "print(\"after\")\nprint(a.stringify())\n", w);
        report("sort-complete", w, r.reason == StopReason::None && line_state(w, "in", 1, 1) &&
               line_state(w, "after", 0, 0) && !w.lines.empty() && w.lines.back() == "[1,2,3]");
    }
    {   // A mutation through an alias inside the comparator: catchable, the sort continues.
        Watch w;
        const VmResult r = watched("a := [3, 1, 2]\nb := a\na.sort((x, y) => {\n"
                                   " try { b.push(9) } catch (e) { print(e.code) }\n"
                                   " try { b[0] = 7 } catch (e) { print(\"set\") }\n"
                                   " try { b.sort((p, q) => p < q) } catch (e) { print(\"nested\") }\n"
                                   " c := [2, 1]\n c.sort((p, q) => p < q)\n"
                                   " return x < y })\nprint(\"after\")\nprint(a.stringify())\n", w);
        report("sort-mutated", w, r.reason == StopReason::None && line_state(w, "runtime.sort_receiver_mutated", 1, 1) &&
               line_state(w, "set", 1, 1) && line_state(w, "nested", 1, 1) && line_state(w, "after", 0, 0) &&
               w.lines.back() == "[1,2,3]");
    }
    {   // A catchable error out of the comparator: drained, released before the handler runs.
        Watch w;
        const VmResult r = watched("a := [3, 1, 2]\ntry { a.sort((x, y) => { print(\"in\")\n throw error(\"boom\") }) }"
                                   " catch (e) { print(\"caught\") }\nprint(a.length())\n", w);
        report("sort-error", w, r.reason == StopReason::None && line_state(w, "in", 1, 1) &&
               line_state(w, "caught", 0, 0) && w.lines.back() == "3");
    }
    {   // The receiver's mutation, uncaught in the comparator, caught outside the sort.
        Watch w;
        const VmResult r = watched("a := [3, 1, 2]\nb := a\ntry { a.sort((x, y) => { print(\"in\")\n b.pop()\n"
                                   " return x < y }) } catch (e) { print(e.code) }\nprint(a.length())\n", w);
        report("sort-mutated-out", w, r.reason == StopReason::None && line_state(w, "in", 1, 1) &&
               line_state(w, "runtime.sort_receiver_mutated", 0, 0) && w.lines.back() == "3");
    }
    {   // A stop inside the comparator (a non-bool answer): released when the run ends.
        Watch w;
        const VmResult r = watched("a := [3, 1, 2]\na.sort((x, y) => { print(\"in\")\n return 1 })\n", w);
        report("sort-stop", w, r.reason == StopReason::Error && line_state(w, "in", 1, 1) &&
               !w.receiver.is_null() && !sort_bit(w.receiver));
    }
    {   // A task's teardown (the production model: an async task runs in its own isolate VM; its
        // sort fails uncaught): by main's catch, the task VM has released it (every VM's count).
        Watch w;
        const std::size_t started = Vm::started_sort_protections();
        const VmResult r = watched("fn[async](t(g)) { g.sort((x, y) => { throw error(\"boom\") })\n"
                                   " return 0 }\nfn(main()) {\n  f := t([3, 1, 2])\n"
                                   "  try { await f } catch (e) { print(\"caught\") }\n  return 0\n}\nmain()\n", w,
                                   nullptr, true);
        bool ok = r.reason == StopReason::None && Vm::started_sort_protections() > started;
        bool seen = false;
        for (std::size_t k = 0; k < w.lines.size(); ++k)
            if (w.lines[k] == "caught") { seen = true; ok = ok && w.live[k] == 0 && w.protections[k] == 0; }
        report("sort-task", w, ok && seen && Vm::live_sort_protections() == 0);
    }
    {   // An exception out of the run (the VM's shutdown path): released when run() exits.
        Watch w;
        w.throw_at = "in";
        bool threw = false;
        watched("a := [3, 1, 2]\na.sort((x, y) => { print(\"in\")\n return x < y })\n", w, &threw);
        report("sort-exception", w, threw && !w.receiver.is_null() && !sort_bit(w.receiver));
    }
    {   // The end of a session statement: the next statement sees the receiver released.
        Watch w;
        VmOptions options;
        options.flush_threshold = 0;
        options.flush = on_flush;
        options.flush_context = &w;
        VmSession session(options);
        w.session = &session;
        std::string out;
        const auto run = [&](const std::string& source) { return session.execute(source, "<session>", out); };
        CHECK(run("a := [3, 1, 2]").execution.reason == StopReason::None);
        const auto stopped = run("a.sort((x, y) => { print(\"in\")\n return 1 })");  // a stop in the comparator
        const Value a = session.value("a");
        report("sort-session", w, stopped.execution.reason == StopReason::Error && line_state(w, "in", 1, -1) &&
               session.sort_protections() == 0 && !sort_bit(a));
        CHECK(run("try { a.sort((x, y) => { print(\"in2\")\n throw error(\"x\") }) } catch (e) { print(\"c2\") }")
                  .execution.reason == StopReason::None);
        report("sort-session-catch", w, line_state(w, "in2", 1, -1) && line_state(w, "c2", 0, -1) &&
               session.sort_protections() == 0 && !sort_bit(session.value("a")));
        CHECK(run("a.push(4)").execution.reason == StopReason::None && array_size(session.value("a")) == 4);
    }
}

} // namespace

int main() {
    CHECK(nift_rt_reference_probe_build[0] != 0);  // the test configuration is linked
    representation();
    snapshots();
    cycles();
    immutability();
    transfer_immutability();
    session_atomicity();
    identity();
    sort_protection();
    return rt_test::finish("refrep-vm");
}
