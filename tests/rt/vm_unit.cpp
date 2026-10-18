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
        {"o := {\"k\": [{\"z\": 4}, {\"z\": 5}]}\nprint(o.k[1].z)\nprint(o[\"k\"][0].z)\n", "5\n4\n"},
        {"fn(g(a)) { a.push(1); return a.length() }\nb := [5]\nprint(g(b))\nprint(b.length())\n", "2\n1\n"},
        {"i := 0\nwhile(i < 9) { i += 1; if(i == 3) { return i } }\n", "3\n"},
        {"k := 1\nif(true) { k := 2; print(k) }\nprint(k)\n", "2\n1\n"},
        // Review round 1.
        {"a := [1, 2]\nprint(a.size())\n", "2\n"},                                       // D3: size() on arrays
        {"x := 0.5 - 0.5\nprint(-x)\nprint(x)\n", "0\n0\n"},                            // D1: a computed zero
        {"i := 0\nwhile(i < 3) { i += 1; if(i == 2) { break } }\nprint(i)\n", "2\n"},   // D2: jump last
        {"fn(f(p)) { return p.length() }\nprint(f([1, 2]))\n", "2\n"},
        // CP12 review round 1, D3: -INT64_MIN is an exact Decimal; print and
        // concatenation render its spelling, as legacy (exit 70 before).
        {"q := 2147483648\nm := (0 - q * q) * 2\nd := -m\nprint(d)\nprint(-d)\nprint(\"s\" + d)\nprint(d + \"s\")\n",
         "9223372036854775808\n-9223372036854775808\ns9223372036854775808\n9223372036854775808s\n"},
        {"q := 2147483648\nm := (0 - q * q) * 2\nd := -m\ni := 0\nwhile (i < 1) { i += 1\nprint(d)\nprint(\"s\" + d) }\n",
         "9223372036854775808\ns9223372036854775808\n"},
        {"fn(f(d)) { return d }\nq := 2147483648\nm := (0 - q * q) * 2\nprint(f(-m))\nprint(-m - 1)\nprint(-m / 2)\n",
         "9223372036854775808\n9.223372036854776e+18\n4611686018427387904\n"},
    };
    for (const Case& c : cases) {
        const rt_vm_tool::Run r = rt_vm_tool::run(c.source);
        CHECK(r.reason.empty());
        CHECK(r.result.reason == StopReason::None);
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

// CP12: run-time errors are reported by the VM itself (legacy's message);
// nothing is replayed. Shapes whose legacy behaviour depends on a run-time
// kind are refused before execution instead (former replay reasons).
void check_errors() {
    struct ErrorCase {
        const char* source;
        StopReason reason;
        const char* message;  // for Error; for None, the output
    };
    const ErrorCase cases[] = {
        {"print(1 / 0)\n", StopReason::Error, "division by zero"},
        {"x := 1\nx = \"s\"\n", StopReason::Error, "cannot assign string to int binding 'x'"},
        {"a := [1]\nprint(a[3])\n", StopReason::Error, "JSON array index 3 is out of range in 'a[3]'"},
        {"x := 4000000000\nprint(x * x)\n", StopReason::Error, "signed 64-bit integer overflow"},
        {"x := 100000000\nprint(x * x)\n", StopReason::None, "10000000000000000\n"},
        {"x := 0\ny := -1\nprint(x * y)\n", StopReason::None, "0\n"},
        {"a := [1]\na[0] = \"s\"\nprint(a[0])\n", StopReason::None, "s\n"},
        {"o := {\"k\": 1}\nprint(o.size())\n", StopReason::None, "1\n"},
        {"o := {\"k\": 1}\nprint(o.length())\n", StopReason::Error, "unknown value or malformed expression: o.length()"},
        {"x := 1\nx = x + 0.5\n", StopReason::Error, "cannot assign double to int binding 'x'"},
        // D3: an integral Decimal index (-INT64_MIN) is out of range, as legacy.
        {"q := 2147483648\nd := -((0 - q * q) * 2)\na := [1]\nprint(a[d])\n", StopReason::Error,
         "JSON array index 9223372036854775808 is out of range in 'a[d]'"},
        {"q := 2147483648\nd := -((0 - q * q) * 2)\na := [1]\na[d] = 2\n", StopReason::Error, "index out of range: d"},
        // D-018a: one number rule everywhere, each statement evaluated once
        // (legacy prints 9007199254740992 for the second, a loop double).
        {"w := 9007199254740991\ni := 0\nwhile(i < 1) { i += 1; q := w + 2; print(q); print(w + 2) }\n",
         StopReason::None, "9007199254740993\n9007199254740993\n"},
        // D-018c: errors at the failing expression, no "return: " prefix.
        {"fn(f(a)) { return a[2] }\nprint(f([1]))\n", StopReason::Error,
         "JSON array index 2 is out of range in 'a[2]'"},
    };
    for (const ErrorCase& c : cases) {
        const rt_vm_tool::Run r = rt_vm_tool::run(c.source);
        CHECK(r.reason.empty());
        CHECK(r.result.reason == c.reason);
        if (c.reason == StopReason::Error) CHECK(r.result.message == c.message);
        else CHECK(r.output == c.message);
        if (r.result.reason != c.reason || (c.reason == StopReason::Error && r.result.message != c.message))
            std::fprintf(stderr, "  program: %s  stop [%s] %s\n", c.source, stop_reason_name(r.result.reason),
                         r.result.message.c_str());
    }
    // Former replay reasons, and context-dependent legacy behaviour (D-018),
    // refused before execution.
    const Case refused[] = {
        {"o := {\"k\": 1}\ni := 0\nwhile(i < 1) { i += 1; print(o.length()) }\n", "context:object-length"},
        {"x := 1\ni := 0\nwhile(i < 2) { x = x + 0.5; i += 1 }\n", "context:assignment-type"},
        // A double-typed binding holding a string: the prepared check fails,
        // the canonical one passes (found by the adversarial gate).
        {"i := 0\nwhile (i < 1) { i += 1\nb0 := \"\" + 1.5\nb1 := b0 + 0.5\nb1 = b0\nprint(b1) }\n",
         "context:assignment-type"},
        {"i := 0\nwhile (i < 1) { i += 1\nv := \"a\" + 1.0\nv = v\nprint(v) }\n", "context:assignment-type"},
        {"x := 0\ni := 0\nwhile(i < 1) { i += 1; print(-x) }\n", "context:negative-zero"},
        // A fused condition takes the comparison's kind check (an ordering
        // of a string and a number reached the VM as an internal failure).
        {"fn(f(p)) {\nif (p > 1) { return 1 }\nreturn 0\n}\nprint(f(\"s\"))\n", "type:>"},
        {"s := \"a\"\nwhile (s < 1) { s = \"b\" }\n", "type:<"},
        {"x := 7.5\ni := 0\nwhile(i < 1) { i += 1; x %= 2 }\n", "context:modulo-assignment"},
        {"s := \"a\" + 0.5\ni := 0\nwhile(i < 1) { i += 1; s += \"b\" }\n", "context:string-append-type"},
        {"fn(f(p)) { p = 1\nreturn p }\ni := 0\nwhile(i < 1) { i += 1; print(f(2.0 * i)) }\n",
         "context:parameter-type"},
        {"a := [1]\nb := a\n", "aliasing"},
        {"fn(f()) { a := [1]\n return a }\nx := f()\n", "aliasing"},
        {"print([1])\n", "type:print"},
        {"s := \"abc\"\nprint(s.size())\n", "type:length"},
        {"x := 5\nprint(x.length())\n", "type:length"},
        {"x := null\nprint(x.size())\n", "type:length"},
        {"print(\"a\" < 1)\n", "type:<"},
        {"s := \"a\"\nprint(-s)\n", "type:unary"},
        {"s := \"a\"\ns += 1\n", "type:compound-assignment"},
        {"print(\"a\" + [1])\n", "type:+"},
        {"o := {\"k\": [1, {\"z\": 4}]}\ni := 1\nprint(o.k[i].z)\n", "type:member"},  // positions only for literal indexes
        {"fn(f(p)) { return p.length() }\no := {\"k\": [1, 2]}\nprint(f(o.k))\n", "path-in-call-argument"},
    };
    for (const Case& c : refused) {
        const rt_vm_tool::Run r = rt_vm_tool::run(c.source);
        CHECK(r.reason == c.output);
        if (r.reason != c.output) std::fprintf(stderr, "  program: %s  reason [%s]\n", c.source, r.reason.c_str());
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
    CHECK(ok.result.reason == StopReason::None && ok.output == "99990\n");
    const char* deeper = "fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) + 1 }\nprint(d(100000))\n";
    const rt_vm_tool::Run too_deep = rt_vm_tool::run(deeper);
    CHECK(too_deep.result.reason == StopReason::Error && too_deep.result.beyond_legacy_depth);
    CHECK(too_deep.result.message == "callable recursion depth exceeded: d");
    VmOptions small;
    small.max_frames = 10;
    CHECK(rt_vm_tool::run("fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) }\nprint(d(9))\n", small).result.reason ==
          StopReason::None);
    CHECK(rt_vm_tool::run("fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) }\nprint(d(10))\n", small).result.reason ==
          StopReason::Error);
}

// Nift-to-Nift calls must not recurse on the C++ stack: 90,000 nested calls
// run on a thread with a 256 KiB stack.
void* deep_on_small_stack(void* out) {
    const rt_vm_tool::Run r = rt_vm_tool::run(
        "fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) + 1 }\nprint(d(90000))\n");
    *static_cast<bool*>(out) = r.result.reason == StopReason::None && r.output == "90000\n";
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

// CP12: output streams through the flush hook (nothing is ever replayed).
void check_streaming() {
    Flushed flushed;
    VmOptions options;
    options.flush_threshold = 16;
    options.flush = &flush_into;
    options.flush_context = &flushed;
    rt_vm_tool::Run r = rt_vm_tool::run("i := 0\nwhile(i < 10) { print(\"line\"); i += 1 }\n", options);
    CHECK(r.result.reason == StopReason::None);
    CHECK(flushed.calls > 0);
    CHECK(flushed.text.size() + r.output.size() == 50);
    // Output before an error stays written (legacy streams it too).
    Flushed before;
    options.flush_context = &before;
    rt_vm_tool::Run e = rt_vm_tool::run("i := 0\nwhile(i < 10) { print(\"line\"); i += 1 }\nprint(1 / 0)\n", options);
    CHECK(e.result.reason == StopReason::Error && before.text.size() + e.output.size() == 50);
}

// Review R1 / round 2 R5: memory is bounded exactly (live rt heap + stacks).
void check_memory_budget() {
    const char* growing =
        "fn(f(n, s)) { t := s + \"xxxxxxxx\"; if (n == 0) { return 0 } return f(n - 1, t) + 1 }\nprint(f(5000, \"a\"))\n";
    VmOptions tight;
    tight.heap_budget = 8 << 20;  // 8 MiB
    rt_vm_tool::Run r = rt_vm_tool::run(growing, tight);
    CHECK(r.result.reason == StopReason::Error);
    CHECK(r.result.beyond_legacy_depth);
    rt_vm_tool::Run fine = rt_vm_tool::run(growing);
    CHECK(fine.result.reason == StopReason::None && fine.output == "5000\n" && fine.result.beyond_legacy_depth);
    VmOptions few;
    few.max_registers = 4096;
    CHECK(rt_vm_tool::run("fn(d(n)) { if(n == 0) { return 0 } return d(n - 1) + 1 }\nprint(d(5000))\n", few)
              .result.reason == StopReason::Error);
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
    CHECK(h.result.reason == StopReason::Error && h.result.beyond_legacy_depth);
    // Exceeding the budget at a depth legacy also reaches is an internal
    // failure (reported, never replayed).
    VmOptions small;
    small.heap_budget = 4 << 20;
    rt_vm_tool::Run flat = rt_vm_tool::run("s := \"abcdefghijklmnopq\"\nk := 0\nwhile(k < 20) { s += s; k += 1 }\n"
                                           "print(s.length())\n", small);
    CHECK(flat.result.reason == StopReason::Internal && !flat.result.beyond_legacy_depth);
    // R6 (CP12): values nested deeper than the source's own literals cannot
    // be built any more (a place holding an aggregate is not copied into a
    // literal: aliasing), so `==` needs no depth bound.
}

// Review round 3. F1: element-assignment targets; F2: comparison chains; F3:
// tree depth bounded before any analysis.
void check_round3_fallbacks() {
    const Case cases[] = {
        {"a := [1, 2, 3]\na[a.length() - 1] = 9\nprint(a[2])\n", "index-target"},
        {"a := [1, 2]\no := {\"k\": 1}\na[o.k] = 9\n", "index-target"},
        {"a := [1, 2]\na[1.0] = 9\n", "index-target"},
        {"fn(g(x)) { return 0 }\na := [1]\na[g(\"x.y\")] = 9\n", "index-target"},
        {"a := [1, 2]\ni := 1\na[(i)] = 9\n", "index-target"},
        {"a := [1, 2]\ni := 0\na[i + 1] = 9\n", "index-target"},
        {"a := [1, 2]\na[0] += 1\n", "compound-target"},
        {"print(1 != 1 == 1)\n", "comparison-chain"},
        {"x := 1\nprint((x < 2) == true)\n", "comparison-chain"},
        {"x := 1\nif (true == (x >= 2)) { print(1) }\n", "comparison-chain"},
        {"x := 1\nwhile ((x < 2) != false) { x += 1 }\n", "comparison-chain"},
    };
    for (const Case& c : cases) {
        const rt_vm_tool::Run r = rt_vm_tool::run(c.source);
        CHECK(r.reason == c.output);
        if (r.reason != c.output) std::fprintf(stderr, "  program: %s  reason [%s]\n", c.source, r.reason.c_str());
    }
    // Admitted forms still compile and run.
    const Case ok[] = {
        {"a := [1, 2, 3]\ni := 2\na[i] = 7\na[0] = 5\na[ i ] = a[i] + 1\nprint(a[0] + a[2])\n", "13\n"},
        {"x := 1\nprint(x < 2)\nprint((x + 1) == 2)\nprint((x < 2) && (x > 0))\n", "true\ntrue\ntrue\n"},
    };
    for (const Case& c : ok) {
        const rt_vm_tool::Run r = rt_vm_tool::run(c.source);
        CHECK(r.reason.empty() && r.output == c.output);
        if (!r.reason.empty() || r.output != c.output)
            std::fprintf(stderr, "  program: %s  got [%s] reason [%s]\n", c.source, r.output.c_str(), r.reason.c_str());
    }
}

std::string chain(const char* head, const char* op, const char* term, int n, const char* tail) {
    std::string s = head;
    for (int k = 0; k < n; ++k) {
        if (k) s += op;
        s += term;
    }
    return s + tail;
}

// No input may exhaust the stack during parsing or compilation: the deep
// inputs are compiled on a thread with a 512 KiB stack.
void* deep_inputs(void* out) {
    const std::string sources[] = {
        chain("print(", " + ", "1", 100000, ")\n"),
        chain("print(", " && ", "true", 100000, ")\n"),
        chain("print(", " || ", "false", 100000, ")\n"),
        chain("print(", " ?? ", "null", 100000, ")\n"),
        chain("fn(f()) { return ", " - ", "1", 100000, " }\nprint(f())\n"),
        chain("i := 0\nwhile (i < 1) { i += 1\nprint(", " + ", "1", 100000, ") }\n"),
        std::string("print(") + std::string(100000, '(') + "1" + std::string(100000, ')') + ")\n",
        std::string("print(") + std::string(100000, '[') + "1" + std::string(100000, ']') + ")\n",
        std::string("print(") + std::string(100000, '-') + "1)\n",
        chain("", "", "if (true) {\n", 3000, "print(1)\n") + chain("", "", "}\n", 3000, ""),
    };
    int refused = 0;
    for (const std::string& s : sources) refused += rt_vm_tool::compile(s).program ? 0 : 1;
    *static_cast<int*>(out) = refused;
    return nullptr;
}

void check_deep_inputs() {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    pthread_t thread;
    int refused = -1;
    CHECK(pthread_create(&thread, &attr, deep_inputs, &refused) == 0);
    pthread_join(thread, nullptr);
    pthread_attr_destroy(&attr);
    CHECK(refused == 10);
    CHECK(rt_vm_tool::compile(chain("print(", " + ", "1", 100000, ")\n")).reason == "nesting-limit");
    // A long flat statement list has no depth and still compiles.
    const rt_vm_tool::Run flat = rt_vm_tool::run(chain("x := 0\n", "", "x += 1\n", 50000, "print(x)\n"));
    CHECK(flat.reason.empty() && flat.output == "50000\n");
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
    // CP12 (D-018): one proto per function, whatever the calling context;
    // flags=1 (kInstrLoopPath) is evidence only.
    CHECK(text.find("Call 4 1 2 flags=1") != std::string::npos);  // arguments in place
    CHECK(text.find("JmpNotLt") != std::string::npos);            // fused loop condition
    CHECK(text.find("AddI 1 1 1 flags=1") != std::string::npos);  // i += 1
    CHECK(text.find("JmpLarge") == std::string::npos);            // no re-evaluation code
    CHECK(c.program->protos.size() == 2);
    CHECK(c.program->protos[1]->nparams == 2);
}

} // namespace

// CP12 review round 1, D2: integer arithmetic at the int64 bounds.
// INT64_MIN % -1 trapped with SIGFPE in the Mod fast path (now 0, as legacy
// and CP09); AddI computed x + offset before its range check (signed
// overflow, UB, near INT64_MIN/MAX). test-rt-vm-sanitize runs these under
// UBSan. Kept self-contained so it applies to CP11 (runtime-2) as well.
void check_integer_bounds() {
    using Stop = decltype(VmResult().reason);  // StopReason (CP12) / BailReason (CP11)
    const char* min = "q := 2147483648\nm := (0 - q * q) * 2\n";  // INT64_MIN
    const Case outputs[] = {
        {"n := 0 - 1\nprint(m % n)\nprint(m % (0 - 1))\nm %= n\nprint(m)\n", "0\n0\n0\n"},
        {"i := 0\nwhile (i < 2) { i += 1\nprint(m % (0 - 1)) }\n", "0\n0\n"},
        {"print(m / (0 - 1))\nprint(m % 7)\nprint(m % (0 - 7))\n", "9.223372036854776e+18\n-1\n-1\n"},
    };
    for (const Case& c : outputs) {
        const std::string source = std::string(min) + c.source;
        const rt_vm_tool::Run r = rt_vm_tool::run(source.c_str());
        CHECK(r.reason.empty() && r.result.reason == Stop::None && r.output == c.output);
        if (r.output != c.output) std::fprintf(stderr, "  program: %s  got [%s]\n", source.c_str(), r.output.c_str());
    }
    {
        const rt_vm_tool::Run r =
            rt_vm_tool::run("fn(f(a, b)) { return a % b }\nq := 2147483648\nprint(f((0 - q * q) * 2, 0 - 1))\n");
        CHECK(r.reason.empty() && r.result.reason == Stop::None && r.output == "0\n");
    }
    const Case errors[] = {
        {"m--\n", "signed 64-bit integer overflow"},
        {"m -= 1\n", "signed 64-bit integer overflow"},
        {"x := 0 - (m + 1)\nx++\n", "signed 64-bit integer overflow"},
        {"x := 0\nprint(5 % x)\n", "modulo by zero"},
        {"x := 0\nprint(5 / x)\n", "division by zero"},
    };
    for (const Case& c : errors) {
        const std::string source = std::string(min) + c.source;
        const rt_vm_tool::Run r = rt_vm_tool::run(source.c_str());
        CHECK(r.reason.empty() && r.result.reason == Stop::Error && r.result.message == c.output);
        if (r.result.message != c.output)
            std::fprintf(stderr, "  program: %s  got [%s]\n", source.c_str(), r.result.message.c_str());
    }
}

int main() {
    check_outputs();
    check_fallbacks();
    check_errors();
    check_literals();
    check_rendering();
    check_frame_budget();
    check_no_native_recursion();
    check_streaming();
    check_memory_budget();
    check_legacy_names();
    check_code_shape();
    check_round3_fallbacks();
    check_deep_inputs();
    check_integer_bounds();
    return rt_test::finish("rt vm unit");
}
