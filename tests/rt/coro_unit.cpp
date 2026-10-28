// Runtime 2.0 CP22: in-process tests of VM coroutines (tasks, futures, the
// scheduler, the wake source). Outputs are the legacy engine's where legacy
// is deterministic (tests/runtime2/cp22 holds the differential corpus); the
// rest pins the VM's own guarantees: exactly-once release (the live heap
// returns to its baseline after every run, abandoned tasks included), timers
// never early, bounded memory, and the host-loop wake path (also under TSan).
#include "rt/coro_wake.h"
#include "rt/heap_account.h"
#include "rt/source_map.h"
#include "rt_test.h"
#include "rt_vm_tool.h"

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace nift::rt;
using Clock = std::chrono::steady_clock;

namespace {

struct Outcome {
    std::string reason;  // compile refusal
    VmResult result;
    std::string output;
};

// Scheduler tests observe scheduling (task output, polls, the clock): they
// compile under the non-contractual observe switch (record §12.1 A5).
CompileLimits scheduler_limits() {
    CompileLimits limits;
    limits.cooperative_observe = true;
    return limits;
}

Outcome run(const std::string& source, VmOptions options = VmOptions()) {
    Outcome o;
    rt_vm_tool::Compiled c = rt_vm_tool::compile(source, scheduler_limits());
    o.reason = c.reason;
    if (!c.program) return o;
    const SourceMap map = SourceMap::single("t.f", source);
    options.source_map = &map;
    Vm vm(*c.program, options);
    o.result = vm.run(o.output);
    return o;
}

// The whole run, compilation included, leaves the live heap where it was
// (interned atoms are charged once per thread and never released: excluded).
std::size_t live_values() { return live_heap_bytes() - heap_charged_bytes(); }

Outcome run_balanced(const std::string& source, const char* label, VmOptions options = VmOptions()) {
    const std::size_t before = live_values();
    Outcome o = run(source, options);
    const std::size_t after = live_values();
    if (after != before) std::fprintf(stderr, "%s: live heap %zu -> %zu\n", label, before, after);
    CHECK(after == before);
    return o;
}

void expect(const std::string& source, const std::string& output, const char* label) {
    const Outcome o = run_balanced(source, label);
    if (!o.reason.empty() || o.output != output || o.result.reason != StopReason::None)
        std::fprintf(stderr, "%s: reason=%s stop=%d message=%s output=[%s]\n", label, o.reason.c_str(),
                     static_cast<int>(o.result.reason), o.result.message.c_str(), o.output.c_str());
    CHECK(o.reason.empty());
    CHECK(o.result.reason == StopReason::None);
    CHECK(o.output == output);
    CHECK(o.result.coro);
}

void check_programs() {
    expect("fn[async](add(a, b)) { return a + b }\np := add(2, 3)\nprint(type(p))\nprint(await p)\n",
           "future\n5\n", "basic");
    expect("g := 1\nfn[async](f()) { g = g + 10; return g }\np := f()\ng = 100\nprint(await p)\nprint(g)\n",
           "11\n100\n", "snapshot isolation");
    expect("fn[async](inner()) { sleep(30); print(\"inner\"); return 1 }\n"
           "fn[async](outer()) { x := inner(); return 2 }\nprint(await outer())\n",
           "inner\n2\n", "structured completion");
    expect("fn[async](f()) { sleep(20); print(\"late\"); return 1 }\np := f()\nprint(\"main\")\n",
           "main\nlate\n", "drain at the end of the run");
    expect("fn[async](f(n)) { sleep(n); return n }\na := f(60)\nb := f(10)\nprint(await b)\nprint(await a)\n",
           "10\n60\n", "sleep order");
    expect("fn[async](bad()) { throw error(\"boom\", \"user.x\") }\np := bad()\n"
           "try { await p } catch(e) { try { await p } catch(e2) { print(e2 == e); print(e2.code); print(e2.line); "
           "print(e2.column) } }\n",
           "true\nuser.x\n1\n20\n", "awaited Error is the original (CP13B A2)");
    expect("fn[async](f()) { return 1 }\np := f()\ni := 0\nseen := false\n"
           "while(i < 5000) { if (p.done()) { seen = true }; i += 1 }\nprint(seen)\n",
           "true\n", "preemption at loop back-edges");
}

// The contractual default refuses the same programs before execution (A5).
void check_default_refuses_observation() {
    const char* print_in_task = "fn[async](f()) { sleep(20); print(\"late\"); return 1 }\np := f()\nprint(\"main\")\n";
    CHECK(rt_vm_tool::compile(print_in_task).reason == "coro-observe:print");
    CHECK(rt_vm_tool::compile("fn[async](f()) { return 1 }\np := f()\nprint(p.done())\n").reason ==
          "coro-observe:done");
    CHECK(rt_vm_tool::compile("fn[async](f()) { return 1 }\nt := epoch()\nprint(await f())\n").reason ==
          "coro-observe:epoch");
    const rt_vm_tool::Compiled pure = rt_vm_tool::compile("fn[async](f(x)) { sleep(5); return x + 1 }\nprint(await f(1))\n");
    CHECK(pure.reason.empty() && pure.program != nullptr);
}

void check_return_in_output() {
    VmOptions options;
    options.return_in_output = true;
    const Outcome o = run_balanced("fn[async](f()) { sleep(20); print(\"task\"); return 1 }\np := f()\nreturn 5\n",
                                   "return before drain", options);
    CHECK(o.output == "5\ntask\n");
    CHECK(!o.result.returned);
    const Outcome kept = run_balanced("fn[async](f()) { return 1 }\np := f()\nreturn 5\n", "return kept");
    CHECK(kept.result.returned && kept.result.return_text == "5");  // embedders keep it separate (CP18)
}

void check_fatal_await() {
    const Outcome o = run_balanced("fn[async](bad()) { return 1 / 0 }\np := bad()\nprint(await p)\n", "fatal");
    CHECK(o.result.reason == StopReason::Error);
    CHECK(o.result.message == "future: division by zero");
    CHECK(o.result.offset > 18 && o.result.offset < 34);  // inside the async function, not at the await
    const Outcome quiet = run_balanced("fn[async](bad()) { return 1 / 0 }\np := bad()\nprint(1)\n", "unawaited");
    CHECK(quiet.result.reason == StopReason::None && quiet.output == "1\n");
}

void check_scale() {
    // 10,000 sleeps of 10 ms: one wait per distinct wake-up, never 10k blocking sleeps.
    const auto t0 = Clock::now();
    const Outcome sleeps = run_balanced(
        "fn[async](f(i)) { sleep(10); return 1 }\nhs := []\ni := 0\n"
        "while(i < 10000) { hs.push(f(i)); i += 1 }\ns := 0\nfor(h : hs) { s += await h }\nprint(s)\n",
        "10k sleeps");
    const double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
    std::printf("coro-unit: 10k sleeps of 10 ms: %.3f s (exploratory, this host)\n", seconds);
    CHECK(sleeps.output == "10000\n");
    CHECK(sleeps.result.coro_spawned == 10000);
    CHECK(seconds < 5.0);  // a gross-regression tripwire only; the gate is measured elsewhere
    const Outcome many = run_balanced(
        "fn[async](t(i)) { return i }\nhs := []\ni := 0\n"
        "while(i < 100000) { hs.push(t(i)); i += 1 }\ns := 0\nfor(h : hs) { s += await h }\nprint(s)\n",
        "100k spawns");
    CHECK(many.output == "4999950000\n");
    CHECK(many.result.coro_spawned == 100000);
}

void check_abandonment() {
    // An internal stop (memory budget) with thousands of suspended tasks:
    // every task is abandoned and released exactly once.
    VmOptions options;
    options.heap_budget = std::size_t(4) << 20;
    const Outcome o = run_balanced(
        "fn[async](f(i)) { x := [i, i, i, i]; sleep(1000); return x.size() }\nhs := []\ni := 0\n"
        "while(i < 1000000) { hs.push(f(i)); i += 1 }\nprint(hs.size())\n",
        "abandoned on memory budget", options);
    if (o.result.reason != StopReason::Internal)
        std::fprintf(stderr, "abandonment: reason=%s stop=%d message=%s output=%zu\n", o.reason.c_str(),
                     static_cast<int>(o.result.reason), o.result.message.c_str(), o.output.size());
    CHECK(o.result.reason == StopReason::Internal);
    CHECK(o.result.message == "memory budget exceeded");
    CHECK(o.output.empty());
    // Failing tasks never awaited: their frames are released at the failure.
    expect("fn[async](f(i)) { t := \"abc\" + i; throw error(\"x\") }\ni := 0\n"
           "while(i < 2000) { f(i); i += 1 }\nprint(\"done\")\n",
           "done\n", "failed tasks released");
}

void check_wake_source() {
    WakeSource wake;
    const auto t0 = Clock::now();
    CHECK(!wake.wait_until(t0 + std::chrono::milliseconds(30)));
    CHECK(Clock::now() - t0 >= std::chrono::milliseconds(30));  // never early
    wake.post();
    const auto t1 = Clock::now();
    CHECK(wake.wait_until(t1 + std::chrono::seconds(10)));  // a pending post returns at once
    CHECK(Clock::now() - t1 < std::chrono::seconds(5));
    std::thread poster([&wake] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        wake.post();
    });
    const auto t2 = Clock::now();
    CHECK(wake.wait_until(t2 + std::chrono::seconds(10)));
    CHECK(Clock::now() - t2 < std::chrono::seconds(5));
    poster.join();
    // The CP16 host loop thread posts at a deadline (the event-loop path).
    auto shared = std::make_shared<WakeSource>();
    const auto t3 = Clock::now();
    shared->post_at(t3 + std::chrono::milliseconds(25));
    CHECK(shared->wait_until(t3 + std::chrono::seconds(10)));
    CHECK(Clock::now() - t3 >= std::chrono::milliseconds(25));
    CHECK(shared->posts() == 1);
    // A source destroyed before its timer fires is never touched.
    {
        auto gone = std::make_shared<WakeSource>();
        gone->post_at(Clock::now() + std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
}

void check_refusals() {
    const struct { const char* source; const char* reason; } cases[] = {
        {"fn(mk()) { n := 1; f := async () => n; return await f() }\nprint(mk())\n", "coro-capture"},
        {"fn[async](add(a, b)) { return a + b }\ng := add\nprint(await g(1, 2))\n", "coro-async-value"},
        {"fn[async](f(a)) { return a }\np := f(1)\nprint(p)\n", "coro-future-flow:Call"},
        {"fn[async](f(a)) { return a }\np := f(1)\ns := \"x\" + p\n", "coro-future-flow:Binary"},
        {"x := 5\nprint(await x)\n", "await-outside-coroutine-unit"},
    };
    for (const auto& c : cases) {
        const Outcome o = run(c.source);
        if (o.reason != c.reason) std::fprintf(stderr, "refusal: %s -> %s\n", c.reason, o.reason.c_str());
        CHECK(o.reason == c.reason);
    }
}

}  // namespace

int main() {
    check_programs();
    check_default_refuses_observation();
    check_return_in_output();
    check_fatal_await();
    check_abandonment();
    check_wake_source();
    check_refusals();
    if (!std::getenv("CORO_UNIT_SKIP_SCALE")) check_scale();
    return rt_test::finish("coro-unit");
}
