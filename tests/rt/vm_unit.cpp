// Runtime 2.0 CP11: unit tests for the compiler and VM (in-process).
// Expected outputs are the legacy engine's (checked by hand against
// v4.12/runtime-2 and, for whole programs, by tests/runtime2/vm_parity.py).
#include "RuntimeValue.h"
#include "rt/aggregate.h"
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
        {"a := [1, 2]\na.push(3)\na[0] = 9\nprint(a[0] + a.length())\n", "12\n"},
        {"o := {\"k\": [1, {\"z\": 4}]}\nprint(o.k[1].z)\nprint(o[\"k\"][0])\n", "4\n1\n"},
        {"fn(g(a)) { a.push(1); return a.length() }\nb := [5]\nprint(g(b))\nprint(b.length())\n", "2\n1\n"},
        {"i := 0\nwhile(i < 9) { i += 1; if(i == 3) { return i } }\n", "3\n"},
        {"k := 1\nif(true) { k := 2; print(k) }\nprint(k)\n", "2\n1\n"},
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
        {"a := [1]\nprint(a [0])\n", "postfix-spelling"},
        {"a := [1,]\n", "trailing-comma"},
        {"o := {\"a\": 1, \"a\": 2}\n", "object-duplicate-key"},
        {"x := 1\nx\n", "expression-statement"},
        {"f := (x) => x\n", "expression:Lambda"},
        {"if(1 { }\n", "parse-error"},
        {"x := 2\nprint(!x + 1)\n", "prefix-not-precedence"},
        {"x := 2\ny := 1\nprint(y && !x || true)\n", "prefix-not-precedence"},
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
        {"x := 1\nx := 2\n", BailReason::StaticError},
        {"fn(f(a)) { return a }\nprint(f())\n", BailReason::StaticError},
        {"a := [1]\na[0] = \"s\"\n", BailReason::Unverified},
        {"print([1])\n", BailReason::Error},
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
    check_code_shape();
    return rt_test::finish("rt vm unit");
}
