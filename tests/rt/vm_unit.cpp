// Runtime 2.0 CP11: unit tests for the compiler and VM (in-process).
// Expected outputs are the legacy engine's (checked by hand against
// v4.12/runtime-2 and, for whole programs, by tests/runtime2/vm_parity.py).
#include "RuntimeValue.h"
#include "rt/aggregate.h"
#include "rt/legacy_names.h"
#include "rt/literal.h"
#include "rt/render.h"
#include "rt/string.h"
#include "rt_test.h"
#include "rt_vm_tool.h"

#include <pthread.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace nift::rt;

namespace {

struct Case {
    const char* source;
    const char* output;
};

void check_outputs() {
    const Case cases[] = {
        {"print(1 + 2)\n", "3\n"},
        {"print(7 / 2)\nprint(-7 % 3)\nprint(10 / 2)\n", "3.5\n-1\n5\n"},
        {"print(0.1 + 0.2)\n", "0.30000000000000004\n"},
        {"print(1.5e-7)\n", "1.5e-07\n"},
        {"print(\"a\" + 1 + 2)\nprint(1 + 2 + \"a\")\n", "a12\n3a\n"},
        {"print(\"x\" + true + null + 1.5)\n", "xtruenull1.5\n"},
        {"x := 1\nx += 2\nx++\nprint(x)\n", "4\n"},
        {"s := \"ab\"\ns += \"cd\"\nprint(s.length())\n", "4\n"},
        {"w := \"h\xc3\xa9llo\"\nprint(w.length())\n", "5\n"},
        {"print(1 && \"a\")\nprint(0 || \"\")\nprint(null ?? 3)\n", "true\nfalse\n3\n"},
        {"x := (2 > 1 ? \"y\" : \"n\")\nprint(x)\n", "y\n"},
        {"print([1, 2] == [1, 2])\nprint(1 == 1.0)\nprint(\"1\" == 1)\n", "true\ntrue\nfalse\n"},
        {"i := 0\nwhile(i < 5) { i += 1; if(i == 2) { continue } if(i == 4) { break } print(i) }\n", "1\n3\n"},
        {"fn(f(n)) { if(n < 2) { return n } return f(n - 1) + f(n - 2) }\nprint(f(15))\n", "610\n"},
        {"fn(f()) { }\nprint(f())\n", "null\n"},
        // `a = t` reads t's binding type (text type of a bare name): its BindType must survive.
        {"fn(g(a, b)) { while(b != 0) { t := b; b = a % b; a = t } return a }\nprint(g(1071, 462))\n", "21\n"},
        {"a := [1, 2]\na.push(3)\na[0] = 9\nprint(a[0] + a.length())\n", "12\n"},
        {"o := {\"k\": [1, {\"z\": 4}]}\nprint(o.k[1].z)\nprint(o[\"k\"][0])\n", "4\n1\n"},
        {"fn(g(a)) { a.push(1); return a.length() }\nb := [5]\nprint(g(b))\nprint(b.length())\n", "2\n1\n"},
        {"i := 0\nwhile(i < 9) { i += 1; if(i == 3) { return i } }\n", "3\n"},
        {"k := 1\nif(true) { k := 2; print(k) }\nprint(k)\n", "2\n1\n"},
        // Review round 1.
        {"a := [1, 2]\nprint(a.size())\n", "2\n"},                                       // D3: size() on arrays
        {"x := 0.5 - 0.5\nprint(-x)\nprint(x)\n", "0\n0\n"},                            // D1: a computed zero
        {"i := 0\nwhile(i < 3) { i += 1; if(i == 2) { break } }\nprint(i)\n", "2\n"},   // D2: jump last
        {"fn(f(p)) { return p.length() }\no := {\"k\": [1, 2]}\nprint(f(o.k))\n", "2\n"},  // D6: read-only callee
    };
    for (const Case& c : cases) {
        const rt_vm_tool::Run r = rt_vm_tool::run(c.source);
        CHECK(r.reason.empty());
        CHECK(r.result.reason == BailReason::None);
        CHECK(r.output == c.output);
        if (r.output != c.output || !r.reason.empty())
            std::fprintf(stderr, "  program: %s  got [%s] reason [%s]\n", c.source, r.output.c_str(), r.reason.c_str());
    }
}

void check_fallbacks() {
    // Since the CP10 review fixes (integrated before CP11), the parser marks
    // several shapes Opaque that the CP11 compiler also refused; the parser's
    // verdict comes first (the compiler checks stay as a second line).
    const Case cases[] = {
        {"x := 1\nprint(\"$[x]\")\n", "interpolation"},
        {"fn(f()) { return n }\nprint(f())\n", "free-name"},
        {"print(1)\nfn(f()) { return 1 }\n", "fn-after-statement"},
        {"const c := 1\n", "declare-const"},
        {"for(x : [1]) { print(x) }\n", "statement:For"},
        {"print(\"a\\tb\")\n", "string-literal"},
        {"print(9007199254740993)\n", "number-literal"},
        {"fn(min(a, b)) { return 0 }\nprint(min(1, 2))\n", "fn-name-reserved"},
        {"x := \"a\"\nprint(x.split(\",\"))\n", "method:split"},
        {"print(args)\n", "unresolved-name"},
        {"a := [1]\nprint(a [0])\n", "opaque:legacy-runtime-error"},
        {"a := [1,]\n", "opaque:legacy-runtime-error"},
        {"o := {\"a\": 1, \"a\": 2}\n", "opaque:legacy-runtime-error"},
        {"x := 1\nx\n", "expression-statement"},
        {"f := (x) => x\n", "expression:Lambda"},
        {"if(1 { }\n", "parse-error"},
        {"x := 2\nprint(!x + 1)\n", "opaque:irregular-legacy-parse"},
        {"x := 2\ny := 1\nprint(y && !x || true)\n", "prefix-not-precedence"},
        // Review round 1. D1: zero-valued decimal spellings keep their text in legacy.
        {"print(0.0)\n", "number-literal"},
        {"x := 0.00\nprint(-x)\n", "number-literal"},
        {"print(0.0e5)\n", "number-literal"},
        {"print(-0.0)\n", "number-literal"},
        {"print(1.0e-300)\n", "number-literal"},  // exponent >= 300: exact spelling in legacy
        // D2: legacy ignores a jump followed by statements in its block.
        {"i := 0\nwhile(i < 3) { i += 1; break; print(\"after\") }\n", "jump-not-last"},
        {"i := 0\nwhile(i < 3) { i += 1; if(true) { continue; print(\"x\") } }\n", "jump-not-last"},
        // D4/D5: names legacy dispatches specially (generated from its source).
        {"fn(is_null(x)) { return 42 }\nprint(is_null(1))\n", "fn-name-reserved"},
        {"fn(is_enum(x)) { return 42 }\nprint(is_enum(1))\n", "fn-name-reserved"},
        {"title := 1\nprint(title)\n", "binding-name-reserved"},
        {"loop := 1\nprint(loop)\n", "binding-name-reserved"},
        {"fn(f(name)) { return name }\nprint(f(1))\n", "param-name-reserved"},
        // D6: a place argument to a parameter the callee mutates (legacy alias).
        {"fn(f(p)) { p.push(9) }\no := {\"k\": [1]}\nf(o.k)\n", "alias-argument"},
        {"fn(f(p)) { p[0] = 5 }\na := [[1]]\nf(a[0])\n", "alias-argument"},
        {"fn(h(z)) { z.push(8) }\nfn(f(p)) { h(p) }\no := {\"k\": [1]}\nf(o.k)\n", "alias-argument"},
        // Review round 2: statically known errors are refused, not replayed.
        {"x := 1\nx := 2\n", "static-error:redeclared"},
        {"fn(f(a)) { return a }\nprint(f())\n", "static-error:arity"},
        {"o := {\"k\": 1}\no.k = 2\n", "static-error:member-assign"},
        {"a := [1]\na.push(1, 2)\n", "static-error:push-arity"},
        // M1/N1: declarations typed from another binding, in prepared-reachable code.
        {"i := 0\nwhile(i < 1) { i += 1; a := 2.0; b := a; b = 2.5 }\n", "prepared-decl-type"},
        {"a := \"s\"\ni := 0\nwhile(i < 1) { i += 1\na := 2.5\nb := (a)\nprint(b) }\n", "prepared-decl-type"},
        {"fn(f(a)) { b := a * 2\nreturn b }\ni := 0\nwhile(i < 1) { print(f(1)); i += 1 }\n", "prepared-decl-type"},
        {"fn(g(a)) { b := a\nreturn b }\nfn(f(a)) { return g(a) }\ni := 0\nwhile(f(i) < 1) { i += 1 }\n",
         "prepared-decl-type"},
        // M2: a loop-called function legacy cannot prepare (arguments evaluated twice).
        {"fn(f(p)) { return (true ? p : 0) }\ni := 0\nwhile(i < 1) { f(1); i += 1 }\n", "prepared-fn-unprepared"},
        {"fn(f(p)) { if (p < 2 == true) { print(1) }\nreturn 0 }\ni := 0\nwhile(i < 1) { f(1); i += 1 }\n",
         "prepared-fn-unprepared"},
        {"fn(f(p)) { if (p > 1) { return }\nreturn 0 }\ni := 0\nwhile(i < 1) { f(1); i += 1 }\n",
         "prepared-fn-unprepared"},
        {"fn(f(p)) { return (p ?? 1) }\ni := 0\nwhile(i < 1) { f(1); i += 1 }\n", "prepared-fn-unprepared"},
        // M3: string literal arguments typed from their text.
        {"fn(f(s)) { return 0 }\nq := null\nprint(f(\"q\"))\n", "string-arg-type"},
        {"fn(f(n)) { return n }\nprint(f(\"2.5\"))\n", "string-arg-type"},
        {"fn(f(n)) { return n }\nprint(f(\"1 + 0.5\"))\n", "string-arg-type"},
    };
    for (const Case& c : cases) {
        const rt_vm_tool::Run r = rt_vm_tool::run(c.source);
        CHECK(r.reason == c.output);
        if (r.reason != c.output) std::fprintf(stderr, "  program: %s  reason [%s]\n", c.source, r.reason.c_str());
    }
}

void check_bails() {
    struct BailCase {
        const char* source;
        BailReason reason;
    };
    const BailCase cases[] = {
        {"print(1 / 0)\n", BailReason::Error},
        {"x := 1\nx = \"s\"\n", BailReason::Error},
        {"a := [1]\nprint(a[3])\n", BailReason::Error},
        {"x := 4000000000\nprint(x * x)\n", BailReason::Error},
        {"x := 100000000\nprint(x * x)\n", BailReason::NumberRange},
        {"x := 0\ny := -1\nprint(x * y)\n", BailReason::NumberRange},
        {"a := [1]\nb := a\n", BailReason::Aliasing},
        {"fn(f()) { a := [1]\n return a }\nx := f()\n", BailReason::Aliasing},
        {"a := [1]\na[0] = \"s\"\n", BailReason::Unverified},
        {"print([1])\n", BailReason::Error},
        // Review round 1. D3: receiver-specific methods.
        {"s := \"abc\"\nprint(s.size())\n", BailReason::Error},
        {"x := 5\nprint(x.length())\n", BailReason::Error},
        {"x := null\nprint(x.size())\n", BailReason::Error},
        {"o := {\"k\": 1}\nprint(o.size())\n", BailReason::Unverified},
        {"o := {\"k\": 1}\nprint(o.length())\n", BailReason::Unverified},
    };
    for (const BailCase& c : cases) {
        const rt_vm_tool::Run r = rt_vm_tool::run(c.source);
        CHECK(r.reason.empty());
        CHECK(r.result.reason == c.reason);
        if (r.result.reason != c.reason)
            std::fprintf(stderr, "  program: %s  bail [%s]\n", c.source, bail_reason_name(r.result.reason));
    }
}

void check_literals() {
    Value v;
    CHECK(decode_number_literal("42", v) && v.is(Kind::Int) && v.as_int() == 42);
    CHECK(decode_number_literal("9007199254740991", v) && v.as_int() == 9007199254740991);
    CHECK(!decode_number_literal("9007199254740992", v));
    CHECK(!decode_number_literal("007", v));
    CHECK(decode_number_literal("1.5", v) && v.is(Kind::Double) && v.as_double() == 1.5);
    CHECK(decode_number_literal(".5", v) && v.as_double() == 0.5);
    CHECK(decode_number_literal("5.", v) && v.as_double() == 5.0);
    CHECK(decode_number_literal("1e3", v) && v.as_double() == 1000.0);
    CHECK(!decode_number_literal("1e300", v));
    CHECK(!decode_number_literal("0.1000000000000000055511151231257827", v));
    CHECK(decode_string_literal("\"abc\"", v) && string_view_of(v) == "abc");
    CHECK(decode_string_literal("'it'", v) && string_view_of(v) == "it");
    CHECK(!decode_string_literal("\"a\\nb\"", v));
    CHECK(!decode_string_literal("\"a$b\"", v));
    CHECK(!decode_string_literal("\"\xff\"", v));
    CHECK(decode_string_literal("\"\xe2\x9c\x93\"", v));
}

// Int rendering must equal legacy's rendering of the same value as a double.
void check_rendering() {
    std::mt19937_64 rng(20261018);
    for (int k = 0; k < 20000; ++k) {
        const std::int64_t i = static_cast<std::int64_t>(rng() % (std::uint64_t(1) << 54)) - (std::int64_t(1) << 53) + 1;
        std::string mine;
        CHECK(append_rendered(Value::integer(i), mine));
        const std::string theirs = nift::RuntimeValue(static_cast<double>(i)).dump(0);
        CHECK(mine == theirs);
        if (mine != theirs) std::fprintf(stderr, "  %s vs %s\n", mine.c_str(), theirs.c_str());
    }
    std::string text;
    CHECK(!append_rendered(make_array(), text));
}

void check_frame_budget() {
    const char* deep = "fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) + 1 }\nprint(d(99990))\n";
    rt_vm_tool::Run ok = rt_vm_tool::run(deep);
    CHECK(ok.result.reason == BailReason::None && ok.output == "99990\n");
    const char* deeper = "fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) + 1 }\nprint(d(100000))\n";
    CHECK(rt_vm_tool::run(deeper).result.reason == BailReason::RecursionBudget);
    VmOptions small;
    small.max_frames = 10;
    CHECK(rt_vm_tool::run("fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) }\nprint(d(9))\n", small).result.reason ==
          BailReason::None);
    CHECK(rt_vm_tool::run("fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) }\nprint(d(10))\n", small).result.reason ==
          BailReason::RecursionBudget);
}

// Nift-to-Nift calls must not recurse on the C++ stack: 90,000 nested calls
// run on a thread with a 256 KiB stack.
void* deep_on_small_stack(void* out) {
    const rt_vm_tool::Run r = rt_vm_tool::run(
        "fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) + 1 }\nprint(d(90000))\n");
    *static_cast<bool*>(out) = r.result.reason == BailReason::None && r.output == "90000\n";
    return nullptr;
}

void check_no_native_recursion() {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    pthread_t thread;
    bool ok = false;
    CHECK(pthread_create(&thread, &attr, deep_on_small_stack, &ok) == 0);
    pthread_join(thread, nullptr);
    pthread_attr_destroy(&attr);
    CHECK(ok);
}

struct Flushed {
    std::string text;
    int calls = 0;
};

void flush_into(std::string& output, void* context) {
    auto* f = static_cast<Flushed*>(context);
    f->text += output;
    ++f->calls;
    output.clear();
}

void check_output_limit() {
    Flushed flushed;
    VmOptions options;
    options.output_limit = 16;
    options.flush = &flush_into;
    options.flush_context = &flushed;
    rt_vm_tool::Run r = rt_vm_tool::run("i := 0\nwhile(i < 10) { print(\"line\"); i += 1 }\n", options);
    CHECK(r.result.reason == BailReason::None);
    CHECK(r.result.committed);
    CHECK(flushed.calls > 0);
    CHECK(flushed.text.size() + r.output.size() == 50);
    rt_vm_tool::Run quiet = rt_vm_tool::run("print(1)\n");
    CHECK(!quiet.result.committed);
    // Review D7: without a flush hook (NIFT_VM=1) the run stops at the limit,
    // nothing is committed and the runner replays the unit on legacy.
    VmOptions bounded;
    bounded.output_limit = 16;
    rt_vm_tool::Run stopped = rt_vm_tool::run("i := 0\nwhile(i < 10) { print(\"line\"); i += 1 }\n", bounded);
    CHECK(stopped.result.reason == BailReason::OutputLimit);
    CHECK(!stopped.result.committed);
}

// Review R1 / round 2 R5: memory is bounded exactly (live rt heap + stacks).
void check_memory_budget() {
    const char* growing =
        "fn(f(n, s)) { t := s + \"xxxxxxxx\"; if (n == 0) { return 0 } return f(n - 1, t) + 1 }\nprint(f(5000, \"a\"))\n";
    VmOptions tight;
    tight.heap_budget = 8 << 20;  // 8 MiB
    rt_vm_tool::Run r = rt_vm_tool::run(growing, tight);
    CHECK(r.result.reason == BailReason::RecursionBudget);
    CHECK(r.result.beyond_legacy_depth);
    rt_vm_tool::Run fine = rt_vm_tool::run(growing);
    CHECK(fine.result.reason == BailReason::None && fine.output == "5000\n" && fine.result.beyond_legacy_depth);
    VmOptions few;
    few.max_registers = 4096;
    CHECK(rt_vm_tool::run("fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) + 1 }\nprint(d(5000))\n", few)
              .result.reason == BailReason::RecursionBudget);
    CHECK(!rt_vm_tool::run("fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) }\nprint(d(63))\n")
               .result.beyond_legacy_depth);
    CHECK(rt_vm_tool::run("fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) }\nprint(d(64))\n")
              .result.beyond_legacy_depth);
    // R5: frames holding [[s]] with distinct ~400 KB strings (nested values,
    // which the old two-level scan missed) stop at the budget, deep.
    const char* hidden =
        "fn(mk(n)) { s := \"B\" + n\nk := 0\nwhile(k < 16) { s += s; k += 1 }\nreturn [[s]] }\n"
        "fn(f(n)) { if(n == 0) { return 0 }\nw := mk(n)\nreturn f(n - 1) + w.length() }\nprint(f(60000))\n";
    VmOptions mid;
    mid.heap_budget = 64 << 20;
    rt_vm_tool::Run h = rt_vm_tool::run(hidden, mid);
    CHECK(h.result.reason == BailReason::RecursionBudget && h.result.beyond_legacy_depth);
    // Exceeding the budget at a depth legacy also reaches is an internal
    // failure (reported, never replayed).
    VmOptions small;
    small.heap_budget = 4 << 20;
    rt_vm_tool::Run flat = rt_vm_tool::run("s := \"abcdefghijklmnopq\"\nk := 0\nwhile(k < 20) { s += s; k += 1 }\n"
                                           "print(s.length())\n", small);
    CHECK(flat.result.reason == BailReason::Internal && !flat.result.beyond_legacy_depth);
    // R6: equality deeper than the VM decides is handed to legacy.
    rt_vm_tool::Run deep = rt_vm_tool::run("fn(w(p)) { return [p ?? 0] }\nx := [1]\ny := [1]\ni := 0\n"
                                           "while(i < 10001) { x = w(x); y = w(y); i += 1 }\nprint(x == y)\n");
    CHECK(deep.result.reason == BailReason::NestingDepth);
    rt_vm_tool::Run shallow = rt_vm_tool::run("fn(w(p)) { return [p ?? 0] }\nx := [1]\ny := [1]\ni := 0\n"
                                              "while(i < 9999) { x = w(x); y = w(y); i += 1 }\nprint(x == y)\n");
    CHECK(shallow.result.reason == BailReason::None && shallow.output == "true\n");
}

void check_legacy_names() {
    for (const char* name : {"is_null", "is_enum", "is_collection", "title", "name", "loop", "print", "error",
                             "build_all", "first", "split", "json"})
        CHECK(legacy_special_name(name));
    for (const char* name : {"fib", "add", "x", "n", "f0", "rec", "total"}) CHECK(!legacy_special_name(name));
    CHECK(legacy_special_name_count() > 150);
}

void check_code_shape() {
    rt_vm_tool::Compiled c = rt_vm_tool::compile(
        "fn(add(a, b)) { return a + b }\ni := 0\ns := 0\nwhile(i < 10) { s = add(s, i); i += 1 }\nprint(s)\n");
    CHECK(c.program != nullptr);
    if (!c.program) return;
    const std::string text = disassemble(*c.program);
    CHECK(text.find("Call 4 1 2") != std::string::npos);    // arguments passed in place
    CHECK(text.find("JmpNotLt") != std::string::npos);      // fused loop condition
    CHECK(text.find("AddI 1 1 1") != std::string::npos);    // i += 1
    CHECK(c.program->protos.size() == 2);
    CHECK(c.program->protos[1]->nparams == 2);
}

} // namespace

int main() {
    check_outputs();
    check_fallbacks();
    check_bails();
    check_literals();
    check_rendering();
    check_frame_budget();
    check_no_native_recursion();
    check_output_limit();
    check_memory_budget();
    check_legacy_names();
    check_code_shape();
    return rt_test::finish("rt vm unit");
}
