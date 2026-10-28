// CP23 milestone 4: shared mutex/atomic/channel registry, cooperative
// cancellation, close/drain sentinel, operator support and cross-worker use.
#include "rt/parallel_task.h"
#include "rt/shared_state.h"
#include "rt/source_map.h"
#include "rt_test.h"
#include "rt_vm_tool.h"

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
    CompileLimits limits;
    limits.parallel_tasks = true;
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

void check_mutex_mutual_exclusion() {
    expect("m := mutex(0)\n"
           "fn[async](bump(n)) { i := 0\n"
           "  while(i < n) { m.lock(); v := m.get(); m.set(v + 1); m.unlock(); i += 1 }\n"
           "  return 0 }\n"
           "a := bump(40)\nb := bump(40)\nc := bump(40)\n"
           "await a\nawait b\nawait c\n"
           "m.lock()\nprint(m.get())\nprint(type(m))\nm.unlock()\n",
           "120\nmutex\n", "mutex mutual exclusion across workers");
}

void check_nested_shared_mutex() {
    expect("m := mutex(0)\n"
           "fn[async](inner()) { m.lock(); v := m.get(); m.set(v + 1); m.unlock(); return 0 }\n"
           "fn[async](outer()) { a := inner(); await a; b := inner(); await b; return 0 }\n"
           "p := outer()\nawait p\nm.lock()\nprint(m.get())\nm.unlock()\n",
           "2\n", "mutex shared by nested tasks");
}

void check_mutex_owner_errors() {
    expect("m := mutex(0)\n"
           "m.lock()\n"
           "try { m.lock() } catch(e) { print(e.code) }\n"
           "fn[async](bad_unlock()) { m.unlock(); return \"no\" }\n"
           "p := bad_unlock()\ntry { await p } catch(e) { print(e.code) }\nm.unlock()\n"
           "try { m.unlock() } catch(e) { print(e.message) }\n"
           "print(m.try_lock())\nprint(m.try_lock())\nm.unlock()\nprint(m.locked())\n",
           "runtime.invalid_argument\nruntime.invalid_argument\nmutex: unlock of unlocked mutex\n"
           "true\nfalse\nfalse\n",
           "mutex recursive/owner/try_lock errors");
}

void check_atomics() {
    expect("a := atomic<int>(0)\n"
           "fn[async](add(n)) { i := 0\n"
           "  while(i < n) { a.fetch_add(1); i += 1 }\n  return 0 }\n"
           "p := add(50)\nq := add(50)\nawait p\nawait q\n"
           "print(a.load())\na += 5\nprint(a.load())\na -= 2\nprint(a.load())\n"
           "print(a++)\nprint(a.load())\nprint(++a)\nprint(a.load())\n"
           "print(a.fetch_sub(1))\nprint(a.load())\n"
           "print(a.compare_exchange(52, 7))\nprint(a.load())\n"
           "print(a.compare_exchange(52, 8))\nprint(a.load())\n"
           "b := atomic<bool>(false)\nprint(b.load())\nprint(b.exchange(true))\n"
           "b.store(false)\nprint(b.compare_exchange(false, true))\nprint(b.load())\n"
           "print(type(a))\nprint(type(b))\n",
           "100\n105\n103\n103\n104\n105\n105\n105\n104\nfalse\n104\nfalse\n104\nfalse\n"
           "false\ntrue\ntrue\natomic<int>\natomic<bool>\n",
           "atomic shared increments, operators and compare_exchange");

    expect("a := atomic<int>(0)\na.store(9223372036854775807)\n"
           "try { a.fetch_add(1) } catch(e) { print(e.code) }\n"
           "try { a += 1 } catch(e) { print(e.code) }\n"
           "try { a++ } catch(e) { print(e.code) }\n"
           "print(a.load())\n",
           "runtime.atomic_overflow\nruntime.atomic_overflow\nruntime.atomic_overflow\n"
           "9223372036854775807\n",
           "atomic overflow is catchable and leaves the value");
}

void check_channels() {
    expect("ch := channel(2)\n"
           "fn[async](produce()) { i := 1\n"
           "  while(i <= 5) { send(ch, i); i += 1 }\n  close(ch); return 0 }\n"
           "p := produce()\ni := 0\nwhile(i < 5) { print(recv(ch)); i += 1 }\n"
           "s := recv(ch)\nprint(s == null)\nprint(type(s))\n"
           "try { send(ch, 9) } catch(e) { print(e.code) }\n"
           "await p\nprint(type(ch))\n",
           "1\n2\n3\n4\n5\nfalse\nerror\nruntime.channel_closed\nchannel\n",
           "bounded channel FIFO, close sentinel and closed send");

    expect("r := channel(0)\n"
           "fn[async](rv()) { i := 0\n"
           "  while(i < 3) { send(r, i); i += 1 }\n  close(r); return 0 }\n"
           "p := rv()\nprint(recv(r))\nprint(recv(r))\nprint(recv(r))\n"
           "print(recv(r) == null)\nawait p\n",
           "0\n1\n2\nfalse\n", "rendezvous channel");

    expect("c := channel(0)\n"
           "fn[async](blocked()) { x := recv(c); return type(x) }\n"
           "p := blocked()\nsleep(20)\nclose(c)\nprint(await p)\n",
           "error\n", "closing a channel wakes blocked receivers");

    expect("ch := channel(1)\n"
           "fn[async](put()) { send(ch, [1, 2, 3]); close(ch); return 0 }\n"
           "p := put()\nx := recv(ch)\nprint(type(x))\nprint(x == null)\nawait p\n",
           "array\nfalse\n", "a channel transfers ordinary aggregates");

    const Outcome invalid = run("try { send(1, 2) } catch(e) { print(e.code) }\n");
    CHECK(invalid.reason.empty());
    CHECK(invalid.result.reason == StopReason::None);
    CHECK(invalid.output == "runtime.invalid_argument\n");
}

void check_invalid_arguments_are_catchable() {
    const Outcome invalid = run("try { atomic<bool>(1) } catch(e) { print(e.code) }\n"
                                "try { channel(-1) } catch(e) { print(e.code) }\n");
    CHECK(invalid.reason.empty());
    CHECK(invalid.result.reason == StopReason::None);
    CHECK(invalid.output == "runtime.invalid_argument\nruntime.invalid_argument\n");
}

void check_cancellation() {
    expect("c := channel(0)\n"
           "fn[async](waiting()) { recv(c); return \"recv\" }\n"
           "p := waiting()\nsleep(20)\ncancel(p)\ncancel(p)\n"
           "try { await p } catch(e) { print(e.code) }\nprint(p.done())\nprint(p.status())\n",
           "runtime.cancelled\ntrue\nerror\n", "cancel wakes a blocked recv idempotently");

    expect("fn[async](sleepy()) { sleep(120); return 1 }\n"
           "fn[async](parent()) { return await sleepy() }\n"
           "p := parent()\nsleep(20)\ncancel(p)\n"
           "try { await p } catch(e) { print(e.code) }\n"
           "try { await p } catch(e) { print(e.code) }\n"
           "print(p.done())\nprint(p.status())\n",
           "runtime.cancelled\nruntime.cancelled\ntrue\nerror\n",
           "cancel wakes a blocked await, exactly-once completion");

    expect("m := mutex(0)\nc := channel(0)\n"
           "fn[async](waiter()) { m.lock()\n"
           "  try { recv(c); m.unlock(); return \"recv\" } catch(e) { m.unlock(); return type(e) } }\n"
           "p := waiter()\nsleep(20)\ncancel(p)\nprint(await p)\nprint(m.locked())\n",
           "error\nfalse\n", "a caught cancellation allows cleanup");

    expect("c := channel(0)\n"
           "fn[async](sender()) { send(c, 1); return \"sent\" }\n"
           "p := sender()\nsleep(20)\ncancel(p)\n"
           "try { await p } catch(e) { print(e.code) }\nprint(p.done())\nprint(p.status())\n",
           "runtime.cancelled\ntrue\nerror\n", "cancel wakes a blocked rendezvous send");

    const Outcome invalid = run("try { cancel(1) } catch(e) { print(e.code) }\n");
    CHECK(invalid.reason.empty());
    CHECK(invalid.result.reason == StopReason::None);
    CHECK(invalid.output == "runtime.invalid_argument\n");
}

void check_registry_releases() {
    shared_registry_clear_for_test();
    CHECK(shared_registry_live_for_test() == 0);
}

}  // namespace

int main() {
    check_mutex_mutual_exclusion();
    check_nested_shared_mutex();
    check_mutex_owner_errors();
    check_atomics();
    check_channels();
    check_invalid_arguments_are_catchable();
    check_cancellation();
    check_registry_releases();
    return rt_test::finish("cp23-shared-unit");
}
