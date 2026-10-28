// CP23 milestone 3: genuine parallel async/thread() on OS workers. Futures
// reference thread-safe records; await/join, structured completion, drain,
// output merge, error transfer and done/status are checked on the parent VM.
#include "rt/parallel_task.h"
#include "rt/source_map.h"
#include "rt_test.h"
#include "rt_vm_tool.h"

#include <chrono>
#include <cstdio>
#include <string>

using namespace nift::rt;

namespace {

struct Outcome {
    std::string reason;
    VmResult result;
    std::string output;
};

Outcome run(const std::string& source) {
    Outcome outcome;
    CompileLimits limits; limits.parallel_tasks = true;
    rt_vm_tool::Compiled compiled = rt_vm_tool::compile(source, limits);
    outcome.reason = compiled.reason;
    if (!compiled.program) return outcome;
    const SourceMap map = SourceMap::single("t.f", source);
    VmOptions options;
    options.source_map = &map;
    options.parallel = ParallelRuntime::from_environment();
    Vm vm(*compiled.program, options);
    outcome.result = vm.run(outcome.output);
    return outcome;
}

void expect(const std::string& source, const std::string& output, const char* label) {
    const Outcome outcome = run(source);
    if (!outcome.reason.empty() || outcome.output != output || outcome.result.reason != StopReason::None)
        std::fprintf(stderr, "%s: reason=%s stop=%d message=%s output=[%s]\n", label, outcome.reason.c_str(),
                     static_cast<int>(outcome.result.reason), outcome.result.message.c_str(),
                     outcome.output.c_str());
    CHECK(outcome.reason.empty());
    CHECK(outcome.result.reason == StopReason::None);
    CHECK(outcome.output == output);
}

void check_arguments_and_globals() {
    expect("g := 5\narr := [10, 20]\n"
           "fn[async](f(a, xs)) { return [a + xs[0] + g, xs[1]] }\n"
           "p := f(3, arr)\ng = 100\narr.push(99)\n"
           "r := await p\nprint(r[0])\nprint(r[1])\nprint(arr.size())\n",
           "18\n20\n3\n", "scalar/array arguments and captured globals");

    expect("g := 1\nfn[async](f()) { g = g + 10; return g }\np := f()\ng = 100\n"
           "print(await p)\nprint(g)\n",
           "11\n100\n", "worker globals are an isolate copy");
}

void check_result_and_repeated_await() {
    expect("fn[async](add(a, b)) { return a + b }\np := add(2, 3)\n"
           "print(await p)\nprint(await p)\nprint(p.done())\nprint(p.status())\n",
           "5\n5\ntrue\ndone\n", "result and repeated await");
}

void check_error_transfer() {
    const Outcome async = run("fn[async](bad()) { return 1 / 0 }\np := bad()\nprint(await p)\n");
    CHECK(async.result.reason == StopReason::Error);
    CHECK(async.result.message == "future: division by zero");
    CHECK(async.result.offset > 18 && async.result.offset < 40);

    const Outcome thread = run("fn(bad()) { return 1 / 0 }\nt := thread(bad)\nprint(t.join())\n");
    CHECK(thread.reason.empty());
    CHECK(thread.result.reason == StopReason::Error);
    CHECK(thread.result.message == "thread: division by zero");

    expect("fn[async](bad()) { throw error(\"boom\", \"user.x\") }\np := bad()\n"
           "try { await p } catch(e) { print(e.message); print(e.code) }\n"
           "try { await p } catch(e2) { print(e2.message); print(e2.code) }\n",
           "boom\nuser.x\nboom\nuser.x\n", "catchable error is the original value");
}

void check_nested_spawn_and_drain() {
    expect("fn[async](inner(x)) { return x + 1 }\n"
           "fn[async](outer(x)) { return await inner(x) }\n"
           "a := outer(1)\nb := outer(2)\nprint(await a)\nprint(await b)\n",
           "2\n3\n", "nested spawn inside a worker");

    expect("fn[async](late()) { sleep(20); print(\"late\"); return 1 }\n"
           "p := late()\nprint(\"main\")\n",
           "main\nlate\n", "unawaited task drains before the top-level run ends");
}

void check_done_status() {
    expect("fn[async](slow()) { sleep(80); return 7 }\np := slow()\n"
           "print(p.status())\nwhile(!p.done()) { }\n"
           "print(p.status())\nprint(await p)\n",
           "running\ndone\n7\n", "done/status reflect the parallel record");
}

void check_thread_prefix_and_join() {
    expect("fn(add(a, b)) { return a + b }\n"
           "fn(nested(x)) { return thread(add, x, 1).join() }\n"
           "t := thread(add, 2, 3)\nprint(t.join())\nprint(t.join())\n"
           "print(thread(nested, 9).join())\nprint(type(t))\n",
           "5\n5\n10\nthread\n", "thread result, repeated join and nested thread");
}

void check_two_worker_overlap() {
    const std::string spin =
        "fn[async](spin(ms)) { t0 := epoch(); n := 0\n"
        "  while(epoch() - t0 < ms) { n = n + 1 }\n  return n }\n";
    const auto measure = [](const std::string& source) {
        CompileLimits limits; limits.parallel_tasks = true;
    rt_vm_tool::Compiled compiled = rt_vm_tool::compile(source, limits);
        CHECK(compiled.reason.empty() && compiled.program != nullptr);
        const SourceMap map = SourceMap::single("t.f", source);
        VmOptions options;
        options.source_map = &map;
        options.parallel = ParallelRuntime::from_environment();
        Vm vm(*compiled.program, options);
        std::string output;
        const auto start = std::chrono::steady_clock::now();
        const VmResult result = vm.run(output);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        CHECK(result.reason == StopReason::None);
        return seconds;
    };
    const std::string one = spin + "print(await spin(300))\n";
    const std::string two = spin + "a := spin(300)\nb := spin(300)\nx := await a\ny := await b\nprint(x + y > 0)\n";
    const double single = measure(one);
    const double pair = measure(two);
    std::printf("cp23-parallel-unit: spin one %.3fs, two %.3fs (ratio %.2f)\n", single, pair, pair / single);
    CHECK(single > 0.15);
    CHECK(pair < single * 1.70);
}

} // namespace

int main() {
    check_arguments_and_globals();
    check_result_and_repeated_await();
    check_error_transfer();
    check_nested_spawn_and_drain();
    check_done_status();
    check_thread_prefix_and_join();
    check_two_worker_overlap();
    return rt_test::finish("cp23-parallel-unit");
}
