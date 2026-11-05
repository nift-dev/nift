// B4b (docs/runtime-2/checkpoints/b4b-element-types.md): the kind inference's
// element and member types under sharing, checked directly on the inference
// (the primary oracle: while the reference guard is on, a stale type cannot
// change an admitted program's output, so end-to-end fixtures cannot see it).
//
// Each case stores a value of one kind through one path and observes the slot
// through another holder; the observing expression's kinds must include the
// stored kind (the type describes what the slot may hold now). Paths: an
// alias (both directions), a member through another reference, an inner
// alias of a nested container, a callee, a closure, a thread argument, a
// reordering mutator, a dynamic receiver (C2), a dynamic key (C3), escaping
// functions (C4) and session-compiled code (C5); wrong-kind swaps (a map where
// a struct was, a struct where a map was).
#include "rt/compiler.h"
#include "rt/globals.h"
#include "rt/infer.h"
#include "rt/parser.h"
#include "rt/resolver.h"
#include "rt_test.h"

#include <memory>
#include <string>
#include <string_view>

using namespace nift::rt;

namespace {

struct Unit {
    std::string source;
    std::unique_ptr<Ast> ast;
    NodeId root = kNoNode;
    UnitPlan plan;
    GlobalPlan globals;
    UnitKinds kinds;
};

std::unique_ptr<Unit> infer(const std::string& source, const SessionCompileContext* session = nullptr) {
    auto u = std::make_unique<Unit>();
    u->source = source;
    u->ast = std::make_unique<Ast>(u->source);
    const ParseResult parsed = parse_script(*u->ast);
    CHECK(parsed.ok);
    u->root = parsed.root;
    u->plan = plan_unit(*u->ast, u->root);
    CHECK(u->plan.ok);
    u->globals = plan_globals(*u->ast, u->root, u->plan);
    u->kinds = infer_unit_kinds(*u->ast, u->root, u->plan, 0, &u->globals, session);
    CHECK(u->kinds.converged);
    return u;
}

// The kinds of the last expression whose source text is `text` (an index,
// member, call or name), or of every name `text` inside the text `scope`.
KindSet last(const Unit& u, std::string_view text) {
    NodeId found = kNoNode;
    for (NodeId id = 0; id < u.ast->size(); ++id) {
        const Node& n = u.ast->at(id);
        if (u.ast->text(n) == text &&
            (found == kNoNode || n.begin >= u.ast->at(found).begin))
            found = id;
    }
    CHECK(found != kNoNode);
    return found == kNoNode ? 0 : u.kinds.of(found);
}


void expect(const char* name, const std::string& source, std::string_view observed, KindSet stored,
            const SessionCompileContext* session = nullptr) {
    const auto u = infer(source, session);
    const KindSet got = last(*u, observed);
    const bool ok = (got & stored) == stored;
    CHECK(ok);
    if (!ok) std::fprintf(stderr, "  case %s: %s has kinds %#llx, missing %#llx\n", name, std::string(observed).c_str(),
                          static_cast<unsigned long long>(got), static_cast<unsigned long long>(stored));
}

}  // namespace

int main() {
    // An element replaced through an alias, observed through the original binding, and the reverse.
    expect("alias", "xs := [1]\nys := xs\nys[0] = \"s\"\nprint(xs[0])\n", "xs[0]", kKindString);
    expect("alias-reverse", "xs := [1]\nys := xs\nxs[0] = \"s\"\nprint(ys[0])\n", "ys[0]", kKindString);
    expect("alias-push", "xs := [1]\nys := xs\nys.push(\"s\")\nprint(xs[1])\n", "xs[1]", kKindString);
    // A wrong-kind swap: a map where a struct was (and a struct where a map was), through an alias.
    expect("swap-map", "struct(C) { a := 1 }\nxs := [C()]\nys := xs\nys[0] = map()\nprint(xs[0])\n", "xs[0]",
           kKindMap);
    expect("swap-struct", "struct(C) { a := 1 }\nxs := [map()]\nys := xs\nys[0] = C()\nprint(xs[0])\n", "xs[0]",
           kKindStruct);
    // An object member changed through a different reference (index and member spellings).
    expect("member", "o := {\"a\": 1}\np := o\np[\"a\"] = \"s\"\nprint(o[\"a\"])\n", "o[\"a\"]", kKindString);
    expect("member-read", "o := {\"a\": 1}\np := o\np[\"a\"] = \"s\"\nprint(o.a)\n", "o.a", kKindString);
    // A nested container mutated through an inner alias.
    expect("inner", "xs := [[1]]\ninner := xs[0]\ninner.push(\"s\")\nprint(xs[0][1])\n", "xs[0][1]", kKindString);
    expect("inner-store", "xs := [[1]]\ninner := xs[0]\ninner[0] = \"s\"\nprint(xs[0][0])\n", "xs[0][0]",
           kKindString);
    expect("inner-object", "o := {\"in\": {\"a\": 1}}\ni := o.in\ni[\"a\"] = \"s\"\nprint(o.in.a)\n", "o.in.a",
           kKindString);
    // Mutation inside a function and inside a closure, observed by the caller.
    expect("callee", "fn(f(a)) { a[0] = \"s\"\nreturn 0 }\nxs := [1]\nf(xs)\nprint(xs[0])\n", "xs[0]", kKindString);
    expect("callee-push", "fn(f(a)) { a.push(\"s\")\nreturn 0 }\nxs := [1]\nf(xs)\nprint(xs[1])\n", "xs[1]",
           kKindString);
    expect("closure", "xs := [1]\ng := () => { xs[0] = \"s\"\nreturn 0 }\ng()\nprint(xs[0])\n", "xs[0]",
           kKindString);
    expect("global", "xs := [1]\nfn(f()) { xs[0] = \"s\"\nreturn 0 }\nf()\nprint(xs[0])\n", "xs[0]", kKindString);
    // A thread's argument (a copy; the sender's sites are a sound over-approximation).
    expect("thread", "fn(w(a)) { a.push(\"s\")\nreturn 0 }\nxs := [1]\nt := thread(w, xs)\nt.join()\nprint(xs[1])\n",
           "xs[1]", kKindString);
    // A reordering mutator through an alias drops the positional facts (B3 N1 per site).
    expect("reverse", "xs := [1, \"a\"]\nys := xs\nys.reverse()\nprint(xs[0])\n", "xs[0]", kKindString);
    expect("sort", "xs := [1, \"a\"]\nys := xs\nys.sort()\nprint(xs[0])\n", "xs[0]", kKindString);
    expect("pop", "xs := [1, \"a\"]\nys := xs\nys.pop()\nys.push(\"b\")\nprint(xs[1])\n", "xs[1]", kKindString);
    // C2: a mutating method on a receiver that may be an array (a union).
    expect("dynamic-receiver",
           "xs := [1]\nv := xs\nif (true) { v = \"q\" }\nv.insert(0, \"s\")\nprint(xs[1])\n", "xs[1]", kKindString);
    // C3: a store under a key that is not a literal reaches every member.
    expect("dynamic-key", "o := {\"a\": 1}\nk := \"a\"\nif (true) { k = \"b\" }\no[k] = \"s\"\nprint(o[\"a\"])\n",
           "o[\"a\"]", kKindString);
    expect("default", "o := {\"a\": 1}\nprint(o.get(\"b\", \"d\"))\n", "o.get(\"b\", \"d\")", kKindString);
    // C4: a function reached by a call whose targets are unknown: its parameters are anything.
    expect("escape-native", "f := (x) => { return x }\nm := mutex(f)\ng := m.get()\nprint(g(\"s\"))\n", "x",
           kKindString);
    // A native's container inside its result (group_by's arrays): one derived site for every read of it.
    expect("native-inner", "xs := [1]\ng := xs.group_by((x) => \"k\")\ninner := g.k\ninner.push(\"s\")\nprint(g.k[1])\n",
           "g.k[1]", kKindString);
    // Review H1: a thread entry of inexact targets (a ternary with an unknown value) widens every
    // function used as a value; so does a call through one, before or after the use.
    expect("thread-unknown-callee",
           "fn(L(a)) { return a.trim() }\nprint(L(\" a \"))\nmx := mutex(1)\n"
           "t := thread(L(\" b \") == \"b\" ? L : mx.get(), 5)\nprint(t.join())\n", "a", kKindInt);
    expect("thread-unknown-returned",
           "fn(L(a)) { return a.trim() }\nprint(L(\" a \"))\nmx := mutex(1)\n"
           "fn(pick()) { return L(\" b \") == \"b\" ? L : mx.get() }\nt := thread(pick(), 5)\nprint(t.join())\n",
           "a", kKindInt);
    expect("call-unknown-ternary",
           "fn(L(a)) { return a.trim() }\nprint(L(\" a \"))\nmx := mutex(1)\n"
           "fn(pick()) { return L(\" b \") == \"b\" ? L : mx.get() }\nf := pick()\nprint(f(5))\n", "a", kKindInt);
    // Review L1 (C2): a union receiver whose object member of the mutator's name is callable.
    expect("union-callable-member",
           "arr := [\" a \"]\no := {\"insert\": (i, x) => { return 0 }}\nv := arr\n"
           "if (arr.size() > 5) { v = o }\nv.insert(0, 5)\nprint(arr[0])\n", "arr[0]", kKindInt);
    // Review H2: more than 16 sites merge into one class; every member's stores stay visible.
    {
        std::string src = "acc := [[1]]\n";
        for (int i = 0; i < 20; ++i) src += "a" + std::to_string(i) + " := [" + std::to_string(i) + "]\nacc.push(a" +
                                             std::to_string(i) + ")\n";
        src += "a3.push(\"s\")\nprint(acc[1][1])\nprint(a7[1])\n";
        expect("collapsed-sites", src, "a7[1]", kKindString);
        expect("collapsed-sites-read", src, "acc[1][1]", kKindString);
    }
    expect("escape-untracked",
           "fn[async](a()) { return (x) => { return x } }\nfa := await a()\nprint(fa(\"s\"))\n", "x", kKindString);
    // C5: in a session statement the statement's own containers are tracked per site like a script's
    // (a function defined in it included); values of earlier statements keep their recorded structure,
    // which the session's shape contracts and coherent recompilation keep true (CP19; the end-to-end
    // fixtures b4b/sessions), and a call into an earlier statement's code makes every container anything.
    SessionCompileContext session;
    expect("session-alias", "xs := [1]\nys := xs\nys[0] = \"s\"\nprint(xs[0])\n", "xs[0]", kKindString, &session);
    expect("session-function", "xs := [1]\nfn(f()) { return xs[0] }\nys := xs\nys[0] = \"s\"\nprint(f())\n",
           "xs[0]", kKindString, &session);
    // C5: a call into code an earlier session statement compiled (`g`, a session callable).
    SessionCompileContext earlier;
    SessionSymbol g;
    g.name = "g";
    g.callable = true;
    g.kinds = kKindFunction;
    g.arity = 1;
    earlier.symbols.push_back(g);
    expect("session-earlier-call", "xs := [1]\ng(xs)\nprint(xs[0])\n", "xs[0]", kKindString, &earlier);
    // ... and one this statement stored into an earlier statement's binding (`old`), which that code can reach.
    SessionSymbol old;
    old.name = "old";
    old.kinds = kKindArray;
    earlier.symbols.push_back(old);
    expect("session-exposed", "ys := [1]\nold = ys\ng(0)\nprint(ys[0])\n", "ys[0]", kKindString, &earlier);
    // Review M1: every path that makes a container reachable by earlier code.
    expect("session-push-exposed", "ys := [1]\nold.push(ys)\ng(0)\nprint(ys[0])\n", "ys[0]", kKindString, &earlier);
    expect("session-index-exposed", "ys := [1]\nold[0] = ys\ng(0)\nprint(ys[0])\n", "ys[0]", kKindString, &earlier);
    expect("session-member-exposed", "ys := [1]\nold.k = ys\ng(0)\nprint(ys[0])\n", "ys[0]", kKindString, &earlier);
    expect("session-closure-exposed", "ys := [1]\nh := () => { return ys }\nold = h\ng(0)\nprint(ys[0])\n",
           "ys[0]", kKindString, &earlier);
    expect("session-late-store", "ys := [1]\nold = ys\nzs := [2]\nys[0] = zs\ng(0)\nprint(zs[0])\n", "zs[0]",
           kKindString, &earlier);
    expect("session-store-after-call", "ys := [1]\ng(0)\nold = ys\nprint(ys[0])\n", "ys[0]", kKindString, &earlier);
    return rt_test::finish("b4b-types-unit");
}
