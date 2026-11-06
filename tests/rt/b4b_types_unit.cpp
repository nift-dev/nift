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
#include "rt/codegen.h"
#include "rt/compiler.h"
#include "rt/globals.h"
#include "rt/infer.h"
#include "rt/infer_unit.h"
#include "rt/parser.h"
#include "rt/resolver.h"
#include "rt_test.h"

#include <dirent.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace nift::rt;

namespace {

struct Unit {
    std::string source;
    std::unique_ptr<Ast> ast;
    NodeId root = kNoNode;
    UnitPlan plan;
    GlobalPlan globals;
    UnitKinds kinds;
    std::vector<NodeId> stale;
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
    // B4b 22: every case is also checked for the invalidation invariant directly: the converged
    // result is a fixed point (one more walk of every unit changes no node's kinds), so no unit
    // kept facts an input change (a class grown, opened or merged) should have re-walked it for.
    infer_detail::Inference inference(*u->ast, u->root, u->plan, &u->globals, session);
    inference.verify_fixed_point();
    u->kinds = inference.run(0);
    CHECK(u->kinds.converged);
    u->stale = inference.stale();
    return u;
}

// B4b 22: the fixed-point check on its own (the stale nodes, with their text, on failure).
void fixed_point(const char* name, const Unit& u) {
    CHECK(u.stale.empty());
    if (!u.stale.empty())
        std::fprintf(stderr, "  case %s: not a fixed point: %zu node(s) change on one more walk, first `%s`\n", name,
                     u.stale.size(), std::string(u.ast->text(u.ast->at(u.stale[0]))).c_str());
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> fixtures(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() > 2 && name.compare(name.size() - 2, 2, ".f") == 0) out.push_back(name);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
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
    fixed_point(name, *u);
}

// B4b 22 (review of 16f24f36): a fixture of tests/runtime2/b4b/widen, where a precise class is
// merged into a class opened before (directly, or A -> B -> C transitively): every reader that
// read the precise class before the merge reads anything after it (each observed expression has
// every kind), and the result is a fixed point.
void widened(const std::string& file, const std::vector<std::string>& observed) {
    const std::string name = "widen-" + file;
    const auto u = infer(read_file("tests/runtime2/b4b/widen/" + file));
    for (const std::string& text : observed) {
        const KindSet got = last(*u, text);
        const bool anything = (got & kKindAll) == kKindAll;
        CHECK(anything);
        if (!anything)
            std::fprintf(stderr, "  case %s: %s has kinds %#llx after the merge into a widened class, not anything\n",
                         name.c_str(), text.c_str(), static_cast<unsigned long long>(got));
    }
    fixed_point(name.c_str(), *u);
}

// F1: the parameter-push shape (`many`: n helpers each pushing a new array into the one array
// they are passed), `nestb` (the pushed array passed in too) and `glob` (a global array).
std::string many_shape(int n) {
    const char* kinds[] = {"1", "\"t\"", "2.5", "true", "null"};
    std::string src;
    for (int i = 0; i < n; ++i)
        src += "fn(k" + std::to_string(i) + "(r)) {\n  r.push([" + kinds[i % 5] + ", \" s \"])\n  return r.size()\n}\n";
    src += "acc := [[\" s \"]]\n";
    for (int i = 0; i < n; ++i) src += "print(k" + std::to_string(i) + "(acc))\n";
    return src + "print(acc[0][1])\n";
}

std::string nestb_shape(int n) {
    const char* values[] = {"[1]", "[\"t\"]", "[[1]]", "[{\"q\": 1}]", "[null]", "[true]", "[2.5]"};
    std::string src;
    for (int i = 0; i < n; ++i)
        src += "fn(k" + std::to_string(i) + "(r, v)) {\n  r.push(v)\n  return r.size()\n}\n";
    src += "acc := [[\" s \"]]\n";
    for (int i = 0; i < n; ++i)
        src += "t" + std::to_string(i) + " := " + values[i % 7] + "\nprint(k" + std::to_string(i) + "(acc, t" +
               std::to_string(i) + "))\n";
    return src + "print(acc[1][0])\n";
}

std::string glob_shape(int n) {
    const char* kinds[] = {"1", "\"t\"", "2.5", "true", "null"};
    std::string src = "acc := [[\" s \"]]\n";
    for (int i = 0; i < n; ++i)
        src += "fn(k" + std::to_string(i) + "()) {\n  acc.push([" + kinds[i % 5] + ", \" s \"])\n  return acc.size()\n}\n";
    for (int i = 0; i < n; ++i) src += "print(k" + std::to_string(i) + "())\n";
    return src + "print(acc[1][1])\n";
}

// F1: the analysis reaches its fixed point for every size (no walk-budget refusal), within a
// linear number of walks; the observed kinds only grow with the size (monotone: precision
// discarded at the 16-class bound never comes back), and past the bound they are anything.
void convergence(const char* name, std::string (*shape)(int), std::string_view observed) {
    KindSet previous = 0;
    for (int n : {8, 14, 15, 16, 17, 18, 24, 40, 100}) {
        const std::string src = shape(n);
        auto u = std::make_unique<Unit>();
        u->source = src;
        u->ast = std::make_unique<Ast>(u->source);
        const ParseResult parsed = parse_script(*u->ast);
        CHECK(parsed.ok);
        u->root = parsed.root;
        u->plan = plan_unit(*u->ast, u->root);
        u->globals = plan_globals(*u->ast, u->root, u->plan);
        infer_detail::Inference inference(*u->ast, u->root, u->plan, &u->globals);
        inference.verify_fixed_point();
        u->kinds = inference.run(0);
        u->stale = inference.stale();
        fixed_point((std::string(name) + "-" + std::to_string(n)).c_str(), *u);
        const std::size_t units = static_cast<std::size_t>(n) + 1;
        const bool converged = u->kinds.converged && u->kinds.walks <= 4 * units + 16;
        CHECK(converged);
        if (!converged)
            std::fprintf(stderr, "  case %s-%d: converged %d after %zu walks (bound %zu)\n", name, n,
                         static_cast<int>(u->kinds.converged), u->kinds.walks, 4 * units + 16);
        if (!u->kinds.converged) continue;
        const KindSet got = last(*u, observed);
        const bool monotone = (got & previous) == previous;
        CHECK(monotone);
        if (!monotone)
            std::fprintf(stderr, "  case %s-%d: %s has kinds %#llx, lost %#llx of the smaller size\n", name, n,
                         std::string(observed).c_str(), static_cast<unsigned long long>(got),
                         static_cast<unsigned long long>(previous & ~got));
        previous = got;
        if (n >= 17) {  // more than 16 origins on the one element union: anything
            const bool anything = (got & kKindAll) == kKindAll;
            CHECK(anything);
            if (!anything)
                std::fprintf(stderr, "  case %s-%d: %s has kinds %#llx, not anything\n", name, n,
                             std::string(observed).c_str(), static_cast<unsigned long long>(got));
        }
    }
}

// P1: FunctionBuilder::emit refuses a register-only operand that carries the constant bit when
// the instruction is built, before any proto exists to run (the compiler then refuses the unit,
// limit:register-operand-constant); register operands and RK operands pass.
void builder_case(const char* name, OpCode op, bool constant_a, bool constant_b, bool rejected) {
    FunctionBuilder fb("p1");
    std::uint16_t r = 0, k = 0;
    CHECK(fb.alloc(r));
    CHECK(fb.constant(Value::integer(5), k));
    fb.emit(op, constant_a ? k : r, constant_b ? k : r, r);
    const bool got = fb.error() == "register-operand-constant";
    CHECK(got == rejected);
    if (got != rejected)
        std::fprintf(stderr, "  case %s: builder error %s, expected %s\n", name,
                     fb.error().empty() ? "none" : fb.error().c_str(), rejected ? "the rejection" : "none");
}

}  // namespace

int main() {
    // B4b 22 (review of 16f24f36): merging a precise class into an already widened one
    // invalidates the precise class's readers (the reviewer's reproducer, single merges through
    // other paths, and A -> B -> C with C widened, both merge orders, functions, lambdas, globals).
    for (const std::string& f : fixtures("tests/runtime2/b4b/widen")) {
        if (f.rfind("transitive_", 0) == 0) widened(f, {"a[0]", "bb[0]"});
        else if (f == "single_object_member.f") widened(f, {"a.k"});
        else if (f == "single_param_reader.f") widened(f, {"v[0]"});
        else widened(f, {"a[0]"});
    }
    CHECK(fixtures("tests/runtime2/b4b/widen").size() >= 27);
    // B4b 22: the fixed point holds for every b4b fixture script too.
    // (a script the planner refuses, before any inference, is left out: `struct_field_container.f`.)
    std::size_t scripts = 0;
    for (const std::string& f : fixtures("tests/runtime2/b4b/scripts")) {
        const std::string src = read_file("tests/runtime2/b4b/scripts/" + f);
        Ast ast(src);
        const ParseResult parsed = parse_script(ast);
        if (!parsed.ok || !plan_unit(ast, parsed.root).ok) {
            std::fprintf(stderr, "  note: fixpoint-%s: not planned (left out)\n", f.c_str());
            continue;
        }
        ++scripts;
        fixed_point(("fixpoint-" + f).c_str(), *infer(src));
    }
    CHECK(scripts >= 30);
    // P1 (review): the operand-classification invariant in the builder.
    builder_case("p1-move-constant-source", OpCode::Move, false, true, true);
    builder_case("p1-loadk-constant-destination", OpCode::LoadK, true, true, true);
    builder_case("p1-add-constant-destination", OpCode::Add, true, false, true);
    builder_case("p1-getindex-constant-destination", OpCode::GetIndex, true, false, true);
    builder_case("p1-move-registers", OpCode::Move, false, false, false);
    builder_case("p1-loadk", OpCode::LoadK, false, true, false);
    builder_case("p1-add-rk-operands", OpCode::Add, false, true, false);
    builder_case("p1-jump-rk-a", OpCode::JmpNotLt, true, true, false);
    // F1: convergence past the 16-class bound (monotone widening to anything).
    convergence("converge-many", many_shape, "acc[0][1]");
    convergence("converge-nestb", nestb_shape, "acc[1][0]");
    convergence("converge-glob", glob_shape, "acc[1][1]");
    // U1 (found with F1, on B4a too): a lambda walked before a later write grows the binding it
    // captures (or a global it reads) is walked again; it must see the int stored there.
    const std::string pre = "fn(t()) { return [1] }\nfn(q()) { return t() }\n";
    expect("lambda-capture-assign", pre + "xs := [\" s \"]\ng := () => { return xs[0] }\nxs = q()\nprint(g())\n",
           "xs[0]", kKindInt);
    expect("lambda-capture-global", pre + "xs := [\" s \"]\ng := () => { return xs[0] }\nfn(w()) { xs = q()\n"
           "return 0 }\nw()\nprint(g())\n", "xs[0]", kKindInt);
    expect("lambda-capture-local", pre + "fn(m()) {\n  xs := [\" s \"]\n  g := () => { return xs[0] }\n"
           "  xs = q()\n  return g()\n}\nprint(m())\n", "xs[0]", kKindInt);
    expect("lambda-capture-param", pre + "fn(m(xs)) {\n  g := () => { return xs[0] }\n  xs = q()\n"
           "  return g()\n}\nprint(m([\" s \"]))\n", "xs[0]", kKindInt);
    expect("lambda-capture-nested", pre + "xs := [\" s \"]\ng := () => { h := () => { return xs[0] }\n"
           "return h() }\nxs = q()\nprint(g())\n", "xs[0]", kKindInt);
    expect("lambda-capture-lambda-write", pre + "xs := [\" s \"]\ng := () => { return xs[0] }\n"
           "w := () => { xs = q()\nreturn 0 }\nw()\nprint(g())\n", "xs[0]", kKindInt);
    expect("lambda-capture-member", "fn(t()) { return {\"a\": 1} }\nfn(q()) { return t() }\n"
           "o := {\"a\": \" s \"}\ng := () => { return o.a }\no = q()\nprint(g())\n", "o.a", kKindInt);
    expect("lambda-capture-callback", pre + "xs := [\" s \"]\nys := [1]\nr := ys.map((v) => xs[0])\nxs = q()\n"
           "print(ys.map((v) => xs[0]))\n", "xs[0]", kKindInt);
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
    // Review H2: a unit that read a site before another unit's store grew it is walked again
    // (r is walked before w: functions are walked last-declared first).
    expect("reader-rewalk",
           "xs := [1]\nfn(w(a)) { a[0] = \"s\"\nreturn 0 }\nfn(r()) { return xs[0] }\nprint(r())\nw(xs)\n", "xs[0]",
           kKindString);
    expect("reader-rewalk-2",
           "xs := [1]\nfn(r()) { return xs[0] }\nfn(w(a)) { a[0] = \"s\"\nreturn 0 }\nprint(r())\nw(xs)\n", "xs[0]",
           kKindString);
    // ... where nothing else re-walks the reader (a lambda reading a captured local; w's return
    // does not grow, so m is not walked again): only the site's readers list.
    expect("reader-rewalk-lambda",
           "fn(m()) {\n  xs := [1]\n  g := () => { return xs[0] }\n  w(xs)\n  return g()\n}\n"
           "fn(w(a)) {\n  a[0] = \"s\"\n  return 0\n}\nprint(m())\n", "xs[0]", kKindString);
    // ... and the stored value grows late (a return chain): only the readers list re-walks the lambda.
    expect("reader-rewalk-late",
           "fn(t()) { return \"s\" }\nfn(q()) { return t() }\nfn(m()) {\n  xs := [1]\n  g := () => { return xs[0] }\n"
           "  w(xs, q())\n  return g()\n}\nfn(w(a, v)) {\n  a[0] = v\n  return 0\n}\nprint(m())\n", "xs[0]", kKindString);
    expect("reader-rewalk-main",
           "xs := [1]\nfn(w(a)) { a[0] = \"s\"\nreturn 0 }\nprint(xs[0])\nw(xs)\n", "xs[0]", kKindString);
    // Owner H1 categories: an escaping function never keeps narrow parameter or capture
    // assumptions unless every target is known (each case calls L or a closure with an int
    // through a path the inference cannot enumerate).
    expect("escape-async-target", "fn(L(a)) { return a.trim() }\nprint(L(\" a \"))\nmx := mutex(1)\nfn[async](runner(f, v)) { return f(v) }\nfn(pick()) { return L(\" b \") == \"b\" ? L : mx.get() }\nr := await runner(pick(), 5)\n", "a", kKindInt);
    expect("escape-callback-native", "fn(L(a)) { return a.trim() }\nprint(L(\" a \"))\nmx := mutex(1)\nm := mutex(L)\ng := m.get()\nprint(g(5))\n", "a", kKindInt);
    expect("escape-returned", "fn(L(a)) { return a.trim() }\nprint(L(\" a \"))\nmx := mutex(1)\nfn(mk()) { return L(\" b \") == \"b\" ? L : mx.get() }\nh := mk()\nprint(h(5))\n", "a", kKindInt);
    expect("escape-stored", "fn(L(a)) { return a.trim() }\nprint(L(\" a \"))\nmx := mutex(1)\no := {\"f\": L}\nm := mutex(o)\nk := m.get()\nq := k.f\nprint(q(5))\n", "a", kKindInt);
    expect("escape-closure", "xs := [\" a \"]\nc := (v) => { return v.trim() }\nprint(c(\" b \"))\nm := mutex(c)\nd := m.get()\nprint(d(5))\n", "v", kKindInt);
    expect("escape-closure-capture", "s := \" a \"\nc := () => { return s.trim() }\nprint(c())\nfn(assign(v)) { s = v\nreturn 0 }\nassign(5)\nprint(c())\n", "s", kKindInt);
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
