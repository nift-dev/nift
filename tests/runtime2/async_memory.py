#!/usr/bin/env python3
"""Async recursion memory safety (C140; D-077 #5 item 2; docs/runtime-2/known-limitations.md).

(c) a nested isolate hands on the original program and the original shared variables, not its own wrapper;
(b) every unfinished isolate is charged its bootstrap bytes plus a fixed overhead against the root heap budget;
(a) a worker thread that cannot be created is an ordinary `limit.exceeded` error (exit 1), never internal (exit 70).

Every run is in a scratch directory, under a hard timeout and an address-space limit (RLIMIT_AS), NIFT_VM=require.
The VM's async results are compared with legacy running the same program made synchronous (`fn[async]` -> `fn`,
`await` removed): legacy refuses an awaited async self-call (rescan-post-cp1.md section 13). thread() cases are
compared with legacy directly.

The per-isolate charge c is measured, not assumed: B(D) is the least NIFT_VM_HEAP_BUDGET under which the recursion
of depth D (D+1 live isolates) succeeds, found by bisection, and must equal (D+1) * c exactly at every depth, which
is also the check that a nested snapshot does not grow (no wrapper image, no wrapper register).

  async_memory.py --nift NIFT [--only SUBSTR]
  async_memory.py --mutants   NIFT_ASYNC_MEMORY_MUTANT=1 (no charge), 2 (completion keeps the charge), 3 (the
                              Internal mapping of a thread-creation failure), 4 (proto operand off by one), 5 (the
                              wrapped image snapshotted again), 6 (an un-awaited cancelled task keeps its charge) and
                              7 (wrapper registers seeded as shared variables), 8 (an un-awaited task's limit
                              failure dropped), 9 (no spawn position for a failed wait on un-awaited children) and
                              10 (a completed task's charge released twice) and 11 (std::bad_alloc creating a
                              task mapped to Internal again, with the fault-injection hook), built by sanctioned_mutant.py, must
                              each fail the cases named in MUTANTS; the mutant directories are deleted afterwards.
                              Mutants 9, 11 and 13-18 keep the fault-injection hook (their cases are injected).

C141 (R1, R2; D-084, D-085): once a run has a fatal resource limit (a task's budget or thread failure noted by the
runtime, or a cleanup wait that cannot start a worker, which starves the runtime: queued tasks fail unrun, new ones
are refused), every shared wait that would block (send, recv, lock) ends the run with exit 1 at its own position,
with the first recorded reason; never exit 70, 134 or a hang. Mutants 13 (the cleanup wait rethrows: terminate),
14 (no sweep of queued tasks), 15 (no refusal once starved), 16 (swept tasks "cancelled", catchable), 17 (the
starving block without its notify: a hang), 18 (a shared wait ignores the fatal limit), 19 (the sweep after
the cleanup block's lock hold, not in it: W1), 20 (the last resumer does not wake the idle workers: S1) and 21 (new
tasks admitted after a noted limit: N1) keep the hook too. The
fault-injection builds also abort (exit 134) when a runtime is destroyed with a charge still reserved
(NIFT_CHECK_DRAINED), so every injected case checks the accounting, and an over-release aborts in every build (C140a).

Exit status of --nift: 0 = every case passes; 1 = a failure. (The C140b exit 3 for the known defect R1 is gone.)
"""
# The fault-injection build (NIFT_ASYNC_MEMORY_MUTANT=12, sanctioned_mutant.py) is made for the alloc-*,
# timer-wait-thread, detached-*, r1-*, r2-*, hang-*, starve-* and first-reason cases and deleted afterwards; run
# through make (HARNESS_TOOLCHAIN_ENV).
import argparse
import os
import resource
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MESSAGE_BUDGET = "parallel task memory budget exceeded"
MESSAGE_THREADS = "parallel task resources exhausted"
AS_LIMIT = 8 << 30
DEFAULT_BUDGET = 1 << 30
BLOCKING_CAPACITY = 1024  # ParallelRuntime::kBlockedCapacity: nested blocked waits (parallel_task.h)

REC = ('fn[async](f(n)) {\n  if (n == 0) {\n    return 0\n  }\n  x := await f(n - 1)\n  return x + n\n}\n'
       'out := await f(%d)\nprint(out)\n')
REC_THREAD = ('fn(f(n)) {\n  if (n == 0) {\n    return 0\n  }\n  t := thread(f, n - 1)\n  return t.join() + n\n}\n'
              'print(thread(f, %d).join())\n')
FUNCS = '''fn(sq(x)) { return x * x }
fn[async](f(n, g)) {
  if (n == 0) {
    return [sq(3), g(5), late(2), held(4), tri(2)]
  }
  r := await f(n - 1, g)
  r.push(sq(n) + late(n) + g(n) + held(n) + tri(n))
  return r
}
fn(dbl(x)) { return x * 2 }
fn(late(x)) { return x * 10 }
held := dbl
out := await f(4, dbl)
print(out.stringify())
'''
FUNCS_THREAD = '''fn(sq(x)) { return x * x }
fn(f(n, g)) {
  if (n == 0) {
    return [sq(3), g(5), late(2), held(4), tri(2)]
  }
  t := thread(f, n - 1, g)
  r := t.join()
  r.push(sq(n) + late(n) + g(n) + held(n) + tri(n))
  return r
}
fn(dbl(x)) { return x * 2 }
fn(late(x)) { return x * 10 }
held := dbl
out := thread(f, 4, dbl).join()
print(out.stringify())
'''
SHARED = '''a := 7
b := "text"
c := [1, 2, 3]
d := 2.5
fn[async](f(n, p, q)) {
  if (n == 0) {
    return [a, b, c, d, e, p, q]
  }
  return await f(n - 1, p + 1, q)
}
e := null
out := await f(5, 100, "arg")
print(out.stringify())
'''
SHARED_THREAD = '''a := 7
b := "text"
c := [1, 2, 3]
d := 2.5
fn(f(n, p, q)) {
  if (n == 0) {
    return [a, b, c, d, e, p, q]
  }
  return thread(f, n - 1, p + 1, q).join()
}
e := null
out := thread(f, 5, 100, "arg").join()
print(out.stringify())
'''
# Neighbouring functions of equal arity: an index off by one calls a neighbour and still exits 0 with another value
# (M4 must be caught by the value, not by a crash or a range error).
EXACT = """fn(one(x)) { return x + 1 }
fn(two(x)) { return x + 20 }
fn(three(x)) { return x + 300 }
fn[async](f(n)) {
  if (n == 0) {
    return one(0) + two(0)
  }
  x := await f(n - 1)
  return x * 10 + one(n) + two(n)
}
fn[async](f2(n)) {
  return 7000
}
fn(four(x)) { return x + 4000 }
out := await f(3)
print(out)
"""
EXACT_THREAD = EXACT.replace("fn[async]", "fn").replace("await f(n - 1)", "thread(f, n - 1).join()").replace(
    "await f(3)", "thread(f, 3).join()")
MODULE = 'fn(tri(x)) { return x * 3 }\nexport(tri)\n'
# Released at completion: sequential awaited tasks under a budget of three charges.
SEQUENTIAL = ('fn[async](g(i)) { return i }\ns := 0\ni := 0\nwhile (i < 200) {\n  s += await g(i)\n  i += 1\n}\n'
              'print(s)\n')
# Released at cancellation (the un-awaited path, ParallelRuntime::cancel_queued): with one CPU permit, each parent
# spawns 4 children without awaiting them and spins until the root cancels it, so its teardown cancels the children
# un-run. Then 8 tasks must fit at once: a leaked charge (4 per round) refuses them.
UNAWAITED = """ready := channel(1)
fn[async](leaf(i)) {
  print("leaf")
  return i
}
fn[async](parent(k)) {
  i := 0
  while (i < k) {
    leaf(i)
    i += 1
  }
  send(ready, 1)
  while (true) {
    i += 1
  }
  return k
}
fn[async](w(i)) {
  sleep(50)
  return i
}
r := 0
while (r < 5) {
  p := parent(4)
  recv(ready)
  cancel(p)
  try {
    await p
  } catch (e) {
    print(e.code)
  }
  r += 1
}
hs := []
i := 0
while (i < 8) {
  hs.push(w(i))
  i += 1
}
s := 0
for (h : hs) {
  s += await h
}
print(s)
"""
WIDTH = ('fn[async](z(i)) {\n  sleep(300)\n  return i\n}\nhs := []\ni := 0\nwhile (i < 40) {\n  hs.push(z(i))\n'
         '  i += 1\n}\nprint("spawned")\ns := 0\nfor (h : hs) {\n  s += await h\n}\nprint(s)\n')
WIDTH_THREAD = ('fn(z(i)) {\n  sleep(300)\n  return i\n}\nhs := []\ni := 0\nwhile (i < 40) {\n'
                '  hs.push(thread(z, i))\n  i += 1\n}\nprint("spawned")\ns := 0\nfor (h : hs) {\n  s += h.join()\n}\n'
                'print(s)\n')
ENQUEUE = 'fn[async](z(i)) {\n  return i\n}\nprint("before")\nz(1)\nprint("after")\n'
ENQUEUE_THREAD = 'fn(z(i)) {\n  return i\n}\nprint("before")\nt := thread(z, 1)\nprint("after")\n'
UNAWAITED_WIDTH = WIDTH.split('print("spawned")')[0].replace("hs.push(z(i))", "z(i)") + 'print("spawned")\n'
UNAWAITED_WIDTH_THREAD = (WIDTH_THREAD.split('print("spawned")')[0].replace("hs.push(thread(z, i))", "thread(z, i)")
                          + 'print("spawned")\n')
# An abandoned task (never awaited, never joined) whose own spawn is refused: the run still ends with the refusal.
ABANDONED = """fn[async](leaf(i)) {
  sleep(300)
  return i
}
fn[async](spawner(k)) {
  i := 0
  while (i < k) {
    leaf(i)
    i += 1
  }
  return k
}
spawner(40)
print("main done")
"""
ABANDONED_THREAD = (ABANDONED.replace("fn[async]", "fn").replace("    leaf(i)", "    thread(leaf, i)")
                    .replace("spawner(40)", "t := thread(spawner, 40)"))
ABANDONED_DEEP = REC.replace("out := await f(%d)\nprint(out)\n", 'f(%d)\nprint("main done")\n')
ABANDONED_DEEP_THREAD = REC_THREAD.replace('print(thread(f, %d).join())\n', 't := thread(f, %d)\nprint("main done")\n')
# An ordinary failure of an un-awaited task: ignored, exit status unaffected (cp22-coroutines.md L7), as on legacy.
ORDINARY = 'fn[async](bad()) {\n  x := 1 / 0\n  return x\n}\nbad()\nprint("end")\n'
ORDINARY_THREAD = 'fn(bad()) {\n  x := 1 / 0\n  return x\n}\nt := thread(bad)\nprint("end")\n'
# Pinned capacity workload: the least budget for k sleeping tasks is exactly k * c.
SLEEPERS = ('fn[async](z(i)) {\n  sleep(500)\n  return i\n}\nhs := []\ni := 0\nwhile (i < %d) {\n  hs.push(z(i))\n'
            '  i += 1\n}\nprint("spawned")\n')
# C140a F1, made deterministic in C140b (T1): a parent whose body is done waits only for its un-awaited child (which
# sleeps, so it is still live); the fault-injection build refuses that wait ("wait:1", the first worker Blocking wait
# of the run, as if no compensating worker could be started). It must be reported at the child's spawn, every run.
DETACHED = ('fn[async](w(n)) {\n  if (n == 0) {\n    sleep(300)\n    return 0\n  }\n  w(n - 1)\n  return 1\n}\n'
            'w(1)\nprint("main done")\n')
DETACHED_THREAD = ('fn(w(n)) {\n  if (n == 0) {\n    sleep(300)\n    return 0\n  }\n  t := thread(w, n - 1)\n'
                   '  return 1\n}\nt := thread(w, 1)\nprint("main done")\n')
# R1 (P1 safety defect, D-082; known, outstanding): a cancelled parent's teardown waits for its still-busy child in
# the noexcept Vm::coro_abort_parallel; when that cleanup wait cannot start a worker ("cleanup:1") the process ends in
# std::terminate (exit 134). The child signals `ready` and then runs 12 long string replacements, so it is inside a
# native call when the root cancels the parent; the parent's teardown reaches the cleanup wait while it still runs.
R1 = """ready := channel(1)
fn[async](leaf(n)) {
  s := "ab"
  i := 0
  while (i < n) {
    s = s + s
    i += 1
  }
  send(ready, 1)
  k := 0
  while (k < 12) {
    s = s.replace("a", "c").replace("c", "a")
    k += 1
  }
  return s.length()
}
fn[async](parent()) {
  t := leaf(22)
  x := await t
  return x
}
print("before")
p := parent()
recv(ready)
cancel(p)
try {
  y := await p
} catch (e) {
  print(e.code)
}
print("after")
"""
# C141 (D-085): the reviewer's sweep routes (hang_a, hang_b), R2 (d1: a budget or thread failure in an un-awaited task,
# then a recv that would block forever), each shared wait after and during a failure (OPS: op 1 recv, 2 send, 3 lock;
# d 0: the failure is recorded first, d 300: the wait is blocked first and woken), concurrent and repeated failures
# (CONC: three budget failures, three blocked workers and the root), the first reason kept (FIRST: a budget failure,
# then a starve), the starve's sweep (SWEPT: a queued task awaited) and refusal (SPAWN: a spawn after it). OPS, CONC
# and FIRST take their op from the first line: op 0 is the calibration run (same text, so the same charges: the
# least budget under which it succeeds is the budget at which the failing spawn of op 1 is the one refused).
HANG_A = """ready := channel(1)
ch := channel(1)
fn[async](leaf(n)) {
  s := "ab"
  i := 0
  while (i < n) {
    s = s + s
    i += 1
  }
  send(ready, 1)
  k := 0
  while (k < 12) {
    s = s.replace("a", "c").replace("c", "a")
    k += 1
  }
  return s.length()
}
fn[async](parent()) {
  t := leaf(22)
  x := await t
  return x
}
fn[async](producer()) {
  send(ch, 42)
  return 0
}
print("before")
p := parent()
recv(ready)
q := producer()
cancel(p)
try {
  y := await p
} catch (e) {
  print(e.code)
}
v := recv(ch)
print(v)
print("after")
"""
HANG_B = """ready := channel(1)
ch := channel(1)
fn[async](leaf(n)) {
  s := "ab"
  i := 0
  while (i < n) {
    s = s + s
    i += 1
  }
  send(ready, 1)
  k := 0
  while (k < 12) {
    s = s.replace("a", "c").replace("c", "a")
    k += 1
  }
  return s.length()
}
fn[async](parent()) {
  t := leaf(22)
  x := await t
  return x
}
fn[async](busy2(n)) {
  i := 0
  while (i < n) {
    i += 1
  }
  return i
}
fn[async](producer()) {
  send(ch, 42)
  return 0
}
print("before")
p := parent()
recv(ready)
b := busy2(3000000)
q := producer()
cancel(p)
try {
  y := await p
} catch (e) {
  print(e.code)
}
v := recv(ch)
print(v)
print("after")
"""
D1 = """ch := channel(1)
fn[async](producer()) {
  send(ch, 42)
  return 0
}
fn[async](helper()) {
  q := producer()
  return 0
}
print("before")
h := helper()
v := recv(ch)
print(v)
print("after")
"""
OPS = """op := 0
d := 0
ch := channel(1)
held := channel(1)
m := mutex(0)
fn[async](child()) {
  return 0
}
fn[async](helper(t, go)) {
  sleep(t)
  if (go != 0) {
    q := child()
  }
  return 0
}
fn[async](holder(mx, go)) {
  mx.lock()
  send(held, 1)
  sleep(go * 700 + 300)
  return 0
}
print("before")
k := holder(m, op)
recv(held)
h := helper(d, op)
if (d == 0) {
  sleep(300)
}
if (op == 1) {
  v := recv(ch)
}
if (op == 2) {
  send(ch, 1)
  send(ch, 2)
}
if (op == 3) {
  m.lock()
}
print("never")
"""
CONC = """op := 0
ch := channel(1)
full := channel(1)
send(full, 0)
m := mutex(0)
m.lock()
fn[async](child()) {
  return 0
}
fn[async](helper(go)) {
  sleep(300)
  if (go != 0) {
    q := child()
  }
  return 0
}
fn[async](rx(c, go)) {
  if (go == 0) {
    sleep(600)
    return 0
  }
  v := recv(c)
  print("rx never")
  return v
}
fn[async](tx(c, go)) {
  if (go == 0) {
    sleep(600)
    return 0
  }
  send(c, 1)
  print("tx never")
  return 0
}
fn[async](locker(mx, go)) {
  if (go == 0) {
    sleep(600)
    return 0
  }
  mx.lock()
  print("lock never")
  return 0
}
print("before")
a := rx(ch, op)
b := tx(full, op)
l := locker(m, op)
h1 := helper(op)
h2 := helper(op)
h3 := helper(op)
if (op != 0) {
  v := recv(ch)
}
print("never")
"""
FIRST = """op := 0
ready := channel(1)
ch := channel(1)
fn[async](leaf(n)) {
  send(ready, 1)
  i := 0
  while (i < n) {
    i += 1
  }
  return i
}
fn[async](parent(go)) {
  t := leaf(go * 27000000 + 3000000)
  x := await t
  return x
}
fn[async](child()) {
  return 0
}
fn[async](helper(go)) {
  if (go != 0) {
    q := child()
  }
  return 0
}
print("before")
p := parent(op)
recv(ready)
h := helper(op)
sleep(300)
cancel(p)
try {
  y := await p
} catch (e) {
  print(e.code)
}
if (op != 0) {
  v := recv(ch)
}
print("never")
"""
# SWEPT (C141a, review T1): the sweep-necessity case (mutant 14), deterministic by state. With 2 workers, `leaf`
# and `b` hold both CPU permits until they are signalled (`leaf` until p's teardown cancels it, `b` until `go`), so
# `q` is queued when p is cancelled. The test-only hook NIFT_TEST_HOLD_COMPLETION (parallel_task_start.cpp, hook
# builds only) holds a finishing task while work is queued and nobody is waiting to resume: `b`'s permit is released
# only once p waits to resume, so p takes it, not `q`; `leaf` cannot finish before the sweep, so p's cleanup waits
# for it and starves the runtime (`cleanup:1`), sweeping `q`. Mutant 14 (no sweep) lets `q` run (`marker`, `7`).
SWEPT = """ready := channel(1)
go := atomic<int>(0)
fn[async](leaf()) {
  send(ready, 1)
  i := 0
  while (i >= 0) {
    i += 1
  }
  return i
}
fn[async](parent()) {
  t := leaf()
  x := await t
  return x
}
fn[async](busy2(flag)) {
  i := 0
  while (flag.load() == 0) {
    i += 1
  }
  return i
}
fn[async](marker()) {
  print("marker")
  return 7
}
print("before")
p := parent()
recv(ready)
b := busy2(go)
q := marker()
cancel(p)
go.store(1)
try {
  y := await p
} catch (e) {
  print(e.code)
}
try {
  v := await q
  print(v)
} catch (e) {
  print(e.code)
}
print("after")
"""
SPAWN = """ready := channel(1)
fn[async](leaf(n)) {
  s := "ab"
  i := 0
  while (i < n) {
    s = s + s
    i += 1
  }
  send(ready, 1)
  k := 0
  while (k < 12) {
    s = s.replace("a", "c").replace("c", "a")
    k += 1
  }
  return s.length()
}
fn[async](parent()) {
  t := leaf(22)
  x := await t
  return x
}
fn[async](marker()) {
  print("marker")
  return 7
}
print("before")
p := parent()
recv(ready)
cancel(p)
try {
  y := await p
} catch (e) {
  print(e.code)
}
z := marker()
x := await z
print(x)
print("after")
"""
# C140a F2: a released charge must be exactly the one charged. One live sleeper, one awaited 1 ms task, then three
# more sleepers under 3.5 charges: the 4th live task (the third sleeper after the await) is refused.
RELEASE = ('fn[async](z(ms)) {\n  sleep(ms)\n  return ms\n}\nb := z(3000)\nx := await z(1)\nc1 := z(3000)\n'
           'c2 := z(3000)\nc3 := z(3000)\nprint("all live")\n')
# C140a F3 (D-081): std::bad_alloc while a task is created (the spawn/start path, the enqueue/submit path) is the
# ordinary limit, at the spawn, with nothing published. Exercised deterministically by the test-only fault-injection
# build (NIFT_ASYNC_MEMORY_MUTANT=12, never production: parallel_task_start.h), in the default environment.
ALLOC = 'fn[async](z(ms)) {\n  sleep(ms)\n  return ms\n}\nprint("before")\nx := await z(1)\nprint("after")\n'
# The 2nd creation (inside an abandoned task) fails; before C141a the root then needed 3 live sleepers under 3.5
# charges (a charge left behind would refuse the third). Since C141a (N1, D-087 #3) the failure is a recorded limit,
# so the root's next spawn (c1, 11:7) is refused with that first reason; a charge left behind by the failed creation
# aborts the fault-injection build at the end of the run (NIFT_CHECK_DRAINED, exit 134), a listed task hangs it.
ALLOC_ROLLBACK = ('fn[async](z(ms)) {\n  sleep(ms)\n  return ms\n}\nfn[async](d()) {\n  y := z(1)\n  return 0\n}\nd()\n'
                  'sleep(300)\nc1 := z(600)\nc2 := z(600)\nc3 := z(600)\nprint("all live")\n')
# C140a F8: a timer wait in a thread() task that cannot start a compensating worker (std::system_error).
TIMER = ('fn(z(ms)) {\n  sleep(ms)\n  return ms\n}\nprint("before")\nt := thread(z, 50)\nx := t.join()\n'
         'print("after")\n')
HOOK = {}
CAUGHT = ('fn[async](f(n)) {\n  if (n == 0) {\n    return 0\n  }\n  x := await f(n - 1)\n  return x + n\n}\n'
          'try {\n  x := await f(40)\n  print(x)\n} catch (e) {\n  print("caught")\n}\nprint("after")\n')


def limits(as_bytes):
    def apply():
        resource.setrlimit(resource.RLIMIT_AS, (as_bytes, as_bytes))
    return apply


def run(nift, src, vm=True, env=None, files=None, as_bytes=AS_LIMIT, timeout=120):
    with tempfile.TemporaryDirectory(prefix="c140-") as d:
        with open(os.path.join(d, "main.f"), "w") as f:
            f.write(src)
        for name, text in (files or {}).items():
            with open(os.path.join(d, name), "w") as f:
                f.write(text)
        e = dict(os.environ, NIFT_VM="require" if vm else "0")
        e.pop("NIFT_VM_HEAP_BUDGET", None)
        e.pop("NIFT_VM_PARALLEL_WORKERS", None)
        e.update(env or {})
        try:
            q = subprocess.run([nift, "main.f"], cwd=d, env=e, capture_output=True, text=True, timeout=timeout,
                               preexec_fn=limits(as_bytes))
        except subprocess.TimeoutExpired:
            return (-1, "", "timeout")
        return (q.returncode, q.stdout, q.stderr.replace(d + "/", ""))


def synchronous(src):
    return src.replace("fn[async]", "fn").replace("await ", "")


def import_module(src):
    return 'import("m.nft")\n' + src


def budget_refused(got, extra=""):
    return got[0] == 1 and got[2].rstrip("\n").endswith(MESSAGE_BUDGET) and "internal" not in got[2] and extra in got[1]


def least_budget(nift, src, problems):
    """The least NIFT_VM_HEAP_BUDGET under which src succeeds; each failing probe must be the budget refusal."""
    lo, hi = 1024, 64 << 20
    if run(nift, src, env={"NIFT_VM_HEAP_BUDGET": str(hi)})[0] != 0:
        problems.append("does not succeed under %d bytes" % hi)
        return None
    if run(nift, src, env={"NIFT_VM_HEAP_BUDGET": str(lo)})[0] == 0:
        problems.append("succeeds under %d bytes: nothing is charged" % lo)
        return None
    while hi - lo > 1:
        mid = (lo + hi) // 2
        got = run(nift, src, env={"NIFT_VM_HEAP_BUDGET": str(mid)})
        if got[0] == 0:
            hi = mid
        else:
            if not budget_refused(got):
                problems.append("budget %d: not the budget refusal: %r" % (mid, got))
                return None
            lo = mid
    return hi


def charge(nift, template, problems, depths=(1, 2, 20, 21)):
    """Per-isolate charge c, and that B(D) == (D + 1) * c at every depth (a constant snapshot)."""
    least = {}
    for depth in depths:
        least[depth] = least_budget(nift, template % depth, problems)
        if least[depth] is None:
            return None
    c = least[2] - least[1]
    for depth, b in least.items():
        if b != (depth + 1) * c:
            problems.append("least budget at depth %d is %d, not (%d + 1) * %d: the per-level charge changes (%r)"
                            % (depth, b, depth, c, least))
    return c if not problems else None


MEASURED = {}


def measured(nift, problems, thread=False):
    key = (nift, thread)
    if key not in MEASURED:
        MEASURED[key] = charge(nift, REC_THREAD if thread else REC, problems)
    elif MEASURED[key] is None:
        problems.append("the per-isolate charge could not be measured (see charge-constant)")
    return MEASURED[key]


def case_values(nift, src, thread, files=None, exact=False):
    want = run(nift, src if thread else synchronous(src), vm=False, files=files)
    got = run(nift, src, files=files)
    if want[0] != 0 or not want[1]:
        return ["legacy oracle failed: %r" % (want,)]
    if got == want:
        return []
    if exact and got[0] == 0:
        return ["exit 0 with another value: vm %r, legacy %r" % (got[1], want[1])]
    return ["vm %r, legacy %r" % (got, want)] + (["(not a value difference)"] if exact else [])


def case_charge(nift, thread):
    problems = []
    c = measured(nift, problems, thread)
    if c is not None:
        print("    per-isolate charge c = %d bytes (%s), B(D) = (D + 1) * c at depths 1, 2, 20, 21"
              % (c, "thread()" if thread else "async"))
    return problems


def case_derived_depth(nift, thread):
    problems = []
    c = measured(nift, problems, thread)
    if c is None:
        return problems
    budget = 50 * c + c // 2
    deepest = budget // c - 1  # D + 1 live isolates, each charged c
    print("    budget %d = 50.5 c: derived maximum depth floor(budget / c) - 1 = %d" % (budget, deepest))
    template = REC_THREAD if thread else REC
    ok = run(nift, template % deepest, env={"NIFT_VM_HEAP_BUDGET": str(budget)})
    if ok != (0, "%d\n" % (deepest * (deepest + 1) // 2), ""):
        problems.append("depth %d: %r" % (deepest, ok))
    over = run(nift, template % (deepest + 1), env={"NIFT_VM_HEAP_BUDGET": str(budget)})
    if not budget_refused(over):
        problems.append("depth %d: %r" % (deepest + 1, over))
    return problems


def case_default_depth(nift):
    problems = []
    c = measured(nift, problems)
    if c is None:
        return problems
    by_budget = DEFAULT_BUDGET // c - 1
    print("    default budget %d: accounting allows depth %d; the %d-wait blocking capacity bounds it first: "
          "derived maximum depth %d" % (DEFAULT_BUDGET, by_budget, BLOCKING_CAPACITY, min(by_budget, BLOCKING_CAPACITY)))
    got = run(nift, REC % 300)
    return [] if got == (0, "45150\n", "") else ["depth 300: %r" % (got,)]


def case_budget(nift, src, want_stdout, budget_charges, env=None):
    problems = []
    c = measured(nift, problems)
    if c is None:
        return problems
    e = {"NIFT_VM_HEAP_BUDGET": str(budget_charges * c)}
    e.update(env or {})
    got = run(nift, src, env=e, timeout=30)  # a leaked charge can refuse a parent its root then waits on forever
    return [] if got == (0, want_stdout, "") else ["%r" % (got,)]


def pin(nift, src, budget_charges, stderr, thread=False, stdout=""):
    """Exact output of a refused spawn: no stdout, exit 1, the documented message at a source position."""
    problems = []
    c = measured(nift, problems, thread)
    if c is None:
        return problems
    got = run(nift, src, env={"NIFT_VM_HEAP_BUDGET": str(budget_charges * c + c // 2)})
    return [] if got == (1, stdout, stderr) else ["want %r, got %r" % ((1, stdout, stderr), got)]


# glibc reserves a malloc arena (64 MiB of address space) per thread; under an address-space limit a malloc can then
# fail before a thread start does, which is std::bad_alloc, an internal failure by the existing policy (exit 70),
# unchanged by C140. One arena makes the thread start the first thing to fail, so these cases test (a) itself.
ONE_ARENA = {"MALLOC_ARENA_MAX": "1"}


def case_threads(nift, src, places, prefix, stdout=""):
    """Depth 400 under a 1 GiB address space: the stacks' address space runs out (8 MiB reserved per thread), at the
    wait that cannot block or at the spawn that cannot start a worker (which comes first depends on scheduling)."""
    got = run(nift, src % 400, env=ONE_ARENA, as_bytes=1 << 30)
    ok = False
    for where in places:
        head = "error: main.f:%s: " % where
        body = got[2][len(head):].rstrip("\n")
        ok = ok or (got[0] == 1 and got[1] == stdout and got[2].startswith(head) and got[2].count("\n") == 1 and
                    body.endswith(MESSAGE_THREADS) and set(body[:-len(MESSAGE_THREADS)].split(prefix)) <= {""})
    return [] if ok else ["depth 400 under a 1 GiB address space: %r" % (got,)]


def case_ordinary(nift, src):
    want = run(nift, src, vm=False)
    got = run(nift, src)
    return [] if got == want == (0, "end\n", "") else ["vm %r, legacy %r" % (got, want)]


def case_capacity(nift):
    """The charge per task is exact and constant: B(200) == 200 * B(1) for the pinned sleeper program, so the number of
    such tasks that can be unfinished at once under a budget B is floor(B / c)."""
    problems = []
    one = least_budget(nift, SLEEPERS % 1, problems)
    if one is None:
        return problems
    many = least_budget(nift, SLEEPERS % 200, problems)
    if many is not None and many != 200 * one:
        problems.append("B(200) = %r, not 200 * B(1) = %d" % (many, 200 * one))
    if not problems:
        print("    pinned sleeper task: c = %d bytes; under the default budget floor(2^30 / c) = %d tasks"
              % (one, DEFAULT_BUDGET // one))
    return problems


def case_detached(nift, src, where, runs=3):
    hook = HOOK.get("binary")
    if not hook:
        return ["no fault-injection build"]
    want = (1, "main done\n", "error: main.f:%s: %s\n" % (where, MESSAGE_THREADS))
    problems = []
    for _ in range(runs):
        got = run(hook, src, env={"NIFT_TEST_PARALLEL_BAD_ALLOC": "wait:1"})
        if got != want:
            problems.append("want %r, got %r" % (want, got))
    return problems


def hooked(src, fault, want, runs=3, workers=None, timeout=30, extra=None):
    """An injected case: `want` (or one of the tuples in a list `want`) in every run."""
    hook = HOOK.get("binary")
    if not hook:
        return ["no fault-injection build"]
    env = {"NIFT_TEST_PARALLEL_BAD_ALLOC": fault} if fault else {}
    if workers:
        env["NIFT_VM_PARALLEL_WORKERS"] = str(workers)
    env.update(extra or {})
    wants = want if isinstance(want, list) else [want]
    got = [run(hook, src, env=env, timeout=timeout) for _ in range(runs)]
    return [] if all(g in wants for g in got) else ["want %r, got %r" % (wants, got)]


def threads_at(where, stdout="before\n", prefix=""):
    return (1, stdout, "error: main.f:%s: %s%s\n" % (where, prefix, MESSAGE_THREADS))


def budget_at(where, stdout="before\n"):
    return (1, stdout, "error: main.f:%s: %s\n" % (where, MESSAGE_BUDGET))


# C141a (M17, the review of C141, Part B): C (`waiter`, a child of P) is woken by the root's send while P (looping) and
# X (`spinner`, the root's task, busy-waiting on the atomic only C sets) hold both permits, so C waits to resume; P is
# cancelled, and its cleanup wait for C, held by the test-only hook until C is waiting to resume, starves the runtime
# (`cleanup:1`). The starving block's notify (M1) resumes C; without it (mutant 17) C, X and P wait forever. X is the
# root's task (P's teardown would cancel a child of P at a loop back-edge, and its completion would wake C); C catches
# the cancellation of its recv by P's teardown and still sets the atomic.
RESUMER = """ch := channel(1)
cready := channel(1)
xready := channel(1)
a := atomic<int>(0)
fn[async](waiter(c, r, flag)) {
  send(r, 1)
  v := 0
  try {
    v = recv(c)
  } catch (e) {
    v = 0
  }
  flag.store(1)
  return v
}
fn[async](spinner(r, flag)) {
  send(r, 1)
  n := 0
  while (flag.load() == 0) {
    n += 1
  }
  return 1
}
fn[async](parent(c, r, flag)) {
  w := waiter(c, r, flag)
  k := 0
  while (k < 100000000) {
    k += 1
  }
  return k
}
print("before")
p := parent(ch, cready, a)
recv(cready)
x := spinner(xready, a)
recv(xready)
send(ch, 1)
cancel(p)
try {
  y := await p
} catch (e) {
  print(e.code)
}
z := await x
print(z)
print("after")
"""


# C141a (W1, D-087 #1; D-088, D-089): supplementary concurrency coverage of the interval between the starve's phases,
# not a mutation control. As SWEPT, then the root keeps spawning (one per millisecond) after the cancel; the test-only
# hook NIFT_TEST_HOLD_PHASE2 holds phase 2 until a submit is refused. Phase 1 recorded the failure in the cleanup
# block's lock hold, so the refusal has the first reason, nothing queued starts, and the run ends once, at the refused
# spawn. It never fails on correct code (30 of 30 runs on the C141a hook build, c141ag/d088-phase-observer.log). It is
# not in MUTANTS: it catches mutant 19 in about 2 of 3 runs only (the queued spawns run, `marker`); the deterministic
# mutation control for W1 (mutant 19) is starve-sweeps-under-lock.
PHASES = """ready := channel(1)
go := atomic<int>(0)
fn[async](leaf()) {
  send(ready, 1)
  i := 0
  while (i >= 0) {
    i += 1
  }
  return i
}
fn[async](parent()) {
  t := leaf()
  x := await t
  return x
}
fn[async](busy2(flag)) {
  i := 0
  while (flag.load() == 0) {
    i += 1
  }
  return i
}
fn[async](marker()) {
  print("marker")
  return 7
}
print("before")
p := parent()
recv(ready)
b := busy2(go)
q := marker()
cancel(p)
go.store(1)
while (true) {
  z := marker()
  sleep(1)
}
"""
# C141a (N1, D-087 #3): a thread failure in an un-awaited task (helper's child, `submit:2`), then the main program's
# later spawn is refused with the first reason. The test-only hook NIFT_TEST_SUBMIT_AFTER_LIMIT makes a spawn by the
# main program wait, while a task is active, until a limit is recorded, so it is the first spawn after the note (and
# the child's is the second submit). Mutant 21 and f448c7b8 admit it: the loop never ends (the timeout).
NOTED = """fn[async](child()) {
  return 0
}
fn[async](helper()) {
  q := child()
  return 0
}
fn[async](later()) {
  return 5
}
print("before")
h := helper()
while (true) {
  z := later()
  w := await z
}
"""
# C141a (S1, D-087 #2): with 2 workers, resumer (R) waits to resume while x and y hold both permits and setter (Q) is
# queued; x and y then block, freeing both permits. The test-only hook NIFT_TEST_S1 (parallel_block.cpp) makes them
# block only while R waits to resume, and R take its permit only once both are free, so every wake-up of the idle
# worker happened while R still counted as resuming. The last resumer's wake-up (S1) starts Q, which sets the atomic
# R busy-waits on; without it (mutant 20) Q never starts and the run never ends.
RESUMING = """f := atomic<int>(0)
gx := atomic<int>(0)
c1 := channel(1)
cx := channel(1)
cy := channel(1)
up := channel(2)
fn[async](resumer(c, flag)) {
  v := recv(c)
  n := 0
  while (flag.load() == 0) {
    n += 1
  }
  return 1
}
fn[async](blocker(go, c, u)) {
  send(u, 1)
  n := 0
  while (go.load() == 0) {
    n += 1
  }
  v := recv(c)
  return v
}
fn[async](setter(flag)) {
  flag.store(1)
  return 2
}
print("before")
r := resumer(c1, f)
x := blocker(gx, cx, up)
y := blocker(gx, cy, up)
recv(up)
recv(up)
q := setter(f)
send(c1, 1)
gx.store(1)
print(await r)
send(cx, 3)
send(cy, 4)
print(await x)
print(await y)
print(await q)
print("after")
"""
CALIBRATED = {}


def calibrated(nift, src, problems):
    """The least budget under which op 0 of src succeeds (cached per binary and program)."""
    key = (nift, src)
    if key not in CALIBRATED:
        CALIBRATED[key] = least_budget(nift, src, problems)
    return CALIBRATED[key]


def case_fatal(nift, src, op, want, d=0, runs=3, hook=False, fault=None):
    """op of src under its calibrated budget: `want` in every run, under a 20 s timeout (a wait that ignores the
    fatal limit blocks forever: mutant 18)."""
    binary = HOOK.get("binary") if hook else nift
    if not binary:
        return ["no fault-injection build"]
    problems = []
    budget = calibrated(binary, src, problems)
    if budget is None:
        return problems
    text = src.replace("op := 0", "op := %d" % op, 1).replace("d := 0", "d := %d" % d, 1)
    env = {"NIFT_VM_HEAP_BUDGET": str(budget)}
    if fault:
        env["NIFT_TEST_PARALLEL_BAD_ALLOC"] = fault
    got = [run(binary, text, env=env, timeout=20) for _ in range(runs)]
    return [] if all(g == want for g in got) else ["budget %d: want %r, got %r" % (budget, want, got)]


def case_d1_budget(nift):
    """R2 as the review found it (d1.f): a budget failure in an un-awaited task, then a recv that would block forever.
    Under one byte less than the least budget the program needs, the producer's spawn is the refused one."""
    problems = []
    need = least_budget(nift, D1, problems)  # every failing probe is the budget refusal (it hung before C141)
    if need is None:
        return problems
    budget = need - 1
    got = [run(nift, D1, env={"NIFT_VM_HEAP_BUDGET": str(budget)}, timeout=20) for _ in range(3)]
    want = budget_at("12:6")
    return [] if all(g == want for g in got) else ["budget %d: want %r, got %r" % (budget, want, got)]


def case_first_reason(nift):
    """The first recorded reason is kept: with a budget failure first and a starve after it the wait reports the
    budget; with the starve alone (a large budget) it reports the resources (so the starve is reached)."""
    problems = case_fatal(nift, FIRST, 1, budget_at("38:8", "before\nruntime.cancelled\n"), hook=True,
                          fault="cleanup:1")
    return problems + hooked(FIRST.replace("op := 0", "op := 1", 1), "cleanup:1",
                             threads_at("38:8", "before\nruntime.cancelled\n"))


# SWEPT without a fault, made schedule-independent (C141a; the reviewer's option (b)): b is released, and b and q are
# awaited before the cancel, so `marker` precedes `7` and `runtime.cancelled` in every run. With SWEPT itself two
# orders occur without a fault, on C140 (0895a234) as on C141 (the reviewer: 45 runs on each binary).
SWEPT_HEALTHY = SWEPT.replace(
    "q := marker()\ncancel(p)\n",
    "q := marker()\ngo.store(1)\nw := await b\nv := await q\nprint(v)\ncancel(p)\n", 1).replace(
    "try {\n  v := await q\n  print(v)\n} catch (e) {\n  print(e.code)\n}\n", "", 1)


def case_healthy(nift):
    """Healthy scheduling is unchanged: the starve programs without a fault, on the production build, exit 0."""
    problems = []
    for name, src, workers, want in (
            ("r1", R1, None, "before\nruntime.cancelled\nafter\n"),
            ("hang_a", HANG_A, 2, "before\nruntime.cancelled\n42\nafter\n"),
            ("hang_b", HANG_B, 2, "before\nruntime.cancelled\n42\nafter\n"),
            ("swept", SWEPT_HEALTHY, 2, "before\nmarker\n7\nruntime.cancelled\nafter\n"),
            ("spawn", SPAWN, None, "before\nruntime.cancelled\nmarker\n7\nafter\n"),
            ("d1", D1, None, "before\n42\nafter\n"),
            ("resuming", RESUMING, 2, "before\n1\n3\n4\n2\nafter\n")):
        got = run(nift, src, env={"NIFT_VM_PARALLEL_WORKERS": str(workers)} if workers else None, timeout=60)
        if got != (0, want, ""):
            problems.append("%s: %r" % (name, got))
    return problems


def case_release(nift):
    problems = []
    one = least_budget(nift, RELEASE.split("x := await")[0] + 'print("all live")\n', problems)
    if one is None:
        return problems
    got = run(nift, RELEASE, env={"NIFT_VM_HEAP_BUDGET": str(3 * one + one // 2)}, timeout=30)
    want = (1, "", "error: main.f:9:7: " + MESSAGE_BUDGET + "\n")
    return [] if got == want else ["want %r, got %r" % (want, got)]


def case_alloc(nift, site, rollback=False):
    hook = HOOK.get("binary")
    if not hook:
        return ["no fault-injection build"]
    fault = {"NIFT_TEST_PARALLEL_BAD_ALLOC": "%s:%d" % (site, 2 if rollback else 1)}
    if not rollback:
        got = run(hook, ALLOC, env=fault)
        want = (1, "before\n", "error: main.f:6:12: %s\n" % MESSAGE_THREADS)
        return [] if got == want else ["want %r, got %r" % (want, got)]
    problems = []
    three = least_budget(hook, ALLOC_ROLLBACK, problems)  # no fault: the peak is the three sleepers, 3 c
    if three is None:
        return problems
    fault["NIFT_VM_HEAP_BUDGET"] = str(three + three // 6)
    got = run(hook, ALLOC_ROLLBACK, env=fault, timeout=30)
    want = (1, "", "error: main.f:11:7: %s\n" % MESSAGE_THREADS)  # C141a (N1): c1 refused, the first reason
    return [] if got == want else ["budget 3.5 c: want %r, got %r" % (want, got)]


def case_timer(nift):
    hook = HOOK.get("binary")
    if not hook:
        return ["no fault-injection build"]
    got = run(hook, TIMER, env={"NIFT_TEST_PARALLEL_BAD_ALLOC": "timer:1"})
    want = (1, "before\n", "error: main.f:6:6: thread: %s\n" % MESSAGE_THREADS)
    return [] if got == want else ["want %r, got %r" % (want, got)]


def build_hook(n):
    out = os.path.join(ROOT, ".build", "mutant-nift", "async-memory-%d" % n)
    b = subprocess.run([sys.executable, "-B", os.path.join(HERE, "sanctioned_mutant.py"), "--out", out,
                        "--define=-DNIFT_ASYNC_MEMORY_MUTANT=%d" % n], capture_output=True, text=True)
    return out, (b.stderr[-400:] or b.stdout[-400:]) if b.returncode != 0 else ""


def case_default_env(nift):
    """F3: without MALLOC_ARENA_MAX, at the configurations shown deterministic here: the first worker thread of a run
    (nothing else allocates per thread yet), and an awaited async recursion of depth 400 under 1 GiB."""
    problems = case_enqueue(nift, ENQUEUE_THREAD, "5:6", env={})
    for _ in range(3):
        got = run(nift, REC % 400, as_bytes=1 << 30)
        if got[0] != 1 or not got[2].rstrip("\n").endswith(MESSAGE_THREADS) or "internal" in got[2]:
            problems.append("depth 400 under 1 GiB, default environment: %r" % (got,))
    return problems


def case_enqueue(nift, src, where, env=None):
    """The first worker thread cannot be created (address space just above the process's own): the spawn itself
    is refused, at the spawn, exit 1; never exit 70, never a silent loss. Swept: the window depends on the host."""
    seen = set()
    problems = []
    for mb in range(12, 41, 2):
        got = run(nift, src, env=ONE_ARENA if env is None else env, as_bytes=mb * 1000 * 1000, timeout=30)
        if got == (0, "before\nafter\n", ""):
            seen.add("ran")
        elif got == (1, "before\n", "error: main.f:%s: %s\n" % (where, MESSAGE_THREADS)):
            seen.add("refused")
        elif got[0] in (1, 127, -6, -11, 134, 139) and "before" not in got[1] and "internal" not in got[2]:
            seen.add("no start")  # too little address space for the process itself (not a task outcome)
        else:
            problems.append("%d MB: %r" % (mb, got))
    if "refused" not in seen:
        problems.append("the enqueue refusal was not reached in 12-40 MB: %r" % sorted(seen))
    return problems


CASES = {
    "functions-async": lambda n: case_values(n, import_module(FUNCS), False, {"m.nft": MODULE}),
    "functions-thread": lambda n: case_values(n, import_module(FUNCS_THREAD), True, {"m.nft": MODULE}),
    "exact-async": lambda n: case_values(n, EXACT, False, exact=True),
    "exact-thread": lambda n: case_values(n, EXACT_THREAD, True, exact=True),
    "shared-async": lambda n: case_values(n, SHARED, False),
    "shared-thread": lambda n: case_values(n, SHARED_THREAD, True),
    "charge-constant-async": lambda n: case_charge(n, False),
    "charge-constant-thread": lambda n: case_charge(n, True),
    "derived-depth-async": lambda n: case_derived_depth(n, False),
    "derived-depth-thread": lambda n: case_derived_depth(n, True),
    "default-depth": case_default_depth,
    "release-completion": lambda n: case_budget(n, SEQUENTIAL, "19900\n", 3),
    "release-unawaited": lambda n: case_budget(n, UNAWAITED, "runtime.cancelled\n" * 5 + "28\n", 12,
                                               {"NIFT_VM_PARALLEL_WORKERS": "1"}),
    "pin-awaited-async": lambda n: pin(n, REC % 40, 10, "error: main.f:5:14: " + "future: " * 10 + MESSAGE_BUDGET + "\n"),
    "pin-awaited-thread": lambda n: pin(n, REC_THREAD % 40, 10, "error: main.f:5:8: " + "thread: " * 10 + MESSAGE_BUDGET
                                        + "\n", True),
    "width-async": lambda n: pin(n, WIDTH, 10, "error: main.f:8:11: " + MESSAGE_BUDGET + "\n"),
    "width-thread": lambda n: pin(n, WIDTH_THREAD, 10, "error: main.f:8:11: " + MESSAGE_BUDGET + "\n"),
    "pin-unawaited-async": lambda n: pin(n, UNAWAITED_WIDTH, 10, "error: main.f:8:3: " + MESSAGE_BUDGET + "\n"),
    "pin-unawaited-thread": lambda n: pin(n, UNAWAITED_WIDTH_THREAD, 10, "error: main.f:8:3: " + MESSAGE_BUDGET + "\n"),
    "pin-not-catchable": lambda n: pin(n, CAUGHT, 10, "error: main.f:5:14: " + "future: " * 10 + MESSAGE_BUDGET + "\n"),
    "threads-async": lambda n: case_threads(n, REC, ("5:8", "5:14"), "future: "),  # the wait, the spawn
    "threads-thread": lambda n: case_threads(n, REC_THREAD, ("6:10", "5:8"), "thread: "),
    "abandoned-async": lambda n: pin(n, ABANDONED, 10, "error: main.f:8:5: " + MESSAGE_BUDGET + "\n", stdout="main done\n"),
    "abandoned-thread": lambda n: pin(n, ABANDONED_THREAD, 10, "error: main.f:8:5: " + MESSAGE_BUDGET + "\n", True,
                                      stdout="main done\n"),
    "abandoned-deep-async": lambda n: case_threads(n, ABANDONED_DEEP, ("5:8", "5:14"), "future: ", "main done\n"),
    "abandoned-deep-thread": lambda n: case_threads(n, ABANDONED_DEEP_THREAD, ("6:10", "5:8"), "thread: ", "main done\n"),
    "ordinary-unawaited-async": lambda n: case_ordinary(n, ORDINARY),
    "ordinary-unawaited-thread": lambda n: case_ordinary(n, ORDINARY_THREAD),
    "capacity": case_capacity,
    "detached-async": lambda n: case_detached(n, DETACHED, "6:3"),
    "detached-thread": lambda n: case_detached(n, DETACHED_THREAD, "6:8"),
    "r1-cleanup-wait": lambda n: hooked(R1, "cleanup:1", threads_at("18:8", "before\nruntime.cancelled\nafter\n")),
    "r2-thread-start": lambda n: hooked(R1, "nothread:2", threads_at("24:1"), workers=2),
    "hang-a": lambda n: hooked(HANG_A, "cleanup:1", [threads_at("19:8", "before\nruntime.cancelled\n42\nafter\n"),
                                                    threads_at("37:6", "before\nruntime.cancelled\n")], workers=2),
    "hang-b": lambda n: hooked(HANG_B, "cleanup:1", [threads_at("45:6", "before\nruntime.cancelled\n"),
                                                    threads_at("19:8", "before\nruntime.cancelled\n42\nafter\n")],
                               workers=2),
    "r2-budget": case_d1_budget,
    "r2-submit": lambda n: hooked(D1, "submit:2", threads_at("12:6")),
    "r2-start": lambda n: hooked(D1, "start:2", threads_at("12:6")),
    "wait-recv-after": lambda n: case_fatal(n, OPS, 1, budget_at("30:8")),
    "wait-recv-woken": lambda n: case_fatal(n, OPS, 1, budget_at("30:8"), d=300),
    "wait-send-after": lambda n: case_fatal(n, OPS, 2, budget_at("34:3")),
    "wait-send-woken": lambda n: case_fatal(n, OPS, 2, budget_at("34:3"), d=300),
    "wait-lock-after": lambda n: case_fatal(n, OPS, 3, budget_at("37:3")),
    "wait-lock-woken": lambda n: case_fatal(n, OPS, 3, budget_at("37:3"), d=300),
    "wait-concurrent": lambda n: case_fatal(n, CONC, 1, budget_at("52:8")),
    "first-reason": case_first_reason,
    "starve-await-swept": lambda n: hooked(SWEPT, "cleanup:1", threads_at("31:6", "before\nruntime.cancelled\n",
                                                                          "future: "), workers=2,
                                           extra={"NIFT_TEST_HOLD_COMPLETION": "1"}),
    "starve-notifies-resumer": lambda n: hooked(RESUMER, "cleanup:1",
                                                threads_at("25:8", "before\nruntime.cancelled\n1\nafter\n"),
                                                workers=2, extra={"NIFT_TEST_CLEANUP_AFTER_RESUMER": "1"}),
    # C141a (W1): the window between the cleanup block's permit release and the starve, held open by the test-only
    # hook NIFT_TEST_STARVE_AFTER_DEQUEUE (parallel_starve.cpp): workers are woken there, so a queued task would start
    # unless the queue was already swept in the same lock hold (mutant 19: the sweep after it, f448c7b8's order).
    "starve-sweeps-under-lock": lambda n: hooked(SWEPT, "cleanup:1", threads_at("31:6", "before\nruntime.cancelled\n",
                                                                                "future: "), workers=2,
                                                 extra={"NIFT_TEST_HOLD_COMPLETION": "1",
                                                        "NIFT_TEST_STARVE_AFTER_DEQUEUE": "1"}),
    "starve-phase-observer": lambda n: hooked(PHASES, "cleanup:1", threads_at("35:8"), workers=2,
                                              extra={"NIFT_TEST_HOLD_COMPLETION": "1", "NIFT_TEST_HOLD_PHASE2": "1"}),
    "refuse-after-note": lambda n: hooked(NOTED, "submit:2", threads_at("14:8"),
                                          extra={"NIFT_TEST_SUBMIT_AFTER_LIMIT": "1"}),
    "resume-wakes-workers": lambda n: hooked(RESUMING, None, (0, "before\n1\n3\n4\n2\nafter\n", ""), workers=2,
                                             extra={"NIFT_TEST_S1": "1"}),
    "starve-spawn-refused": lambda n: hooked(SPAWN, "cleanup:1", threads_at("35:6", "before\nruntime.cancelled\n")),
    "healthy-unchanged": case_healthy,
    "release-exact": case_release,
    "threads-default-environment": case_default_env,
    "alloc-start": lambda n: case_alloc(n, "start"),
    "alloc-submit": lambda n: case_alloc(n, "submit"),
    "alloc-rollback-start": lambda n: case_alloc(n, "start", True),
    "alloc-rollback-submit": lambda n: case_alloc(n, "submit", True),
    "timer-wait-thread": case_timer,
    "enqueue-async": lambda n: case_enqueue(n, ENQUEUE, "5:1"),
    "enqueue-thread": lambda n: case_enqueue(n, ENQUEUE_THREAD, "5:6"),
}

MUTANTS = {
    1: ["derived-depth-async", "width-async", "width-thread", "pin-unawaited-async", "pin-unawaited-thread"],
    2: ["release-completion"],
    3: ["threads-async", "threads-thread", "enqueue-async", "enqueue-thread"],
    4: ["exact-async", "exact-thread", "functions-async", "functions-thread"],
    5: ["charge-constant-async", "charge-constant-thread"],
    6: ["release-unawaited"],
    7: ["charge-constant-async", "charge-constant-thread"],
    8: ["abandoned-async", "abandoned-thread", "abandoned-deep-async", "abandoned-deep-thread"],
    9: ["detached-async", "detached-thread"],
    10: ["release-exact", "release-completion"],
    11: ["alloc-start", "alloc-submit", "alloc-rollback-start", "alloc-rollback-submit"],
    13: ["r1-cleanup-wait", "starve-spawn-refused"],
    14: ["starve-await-swept"],
    15: ["starve-spawn-refused"],
    16: ["starve-await-swept"],
    17: ["starve-notifies-resumer"],
    18: ["r2-budget", "r2-submit", "wait-recv-woken", "wait-send-after", "wait-lock-woken", "wait-concurrent"],
    19: ["starve-sweeps-under-lock"],
    20: ["resume-wakes-workers"],
    21: ["refuse-after-note"],
}


PROBLEMS = {}
VALUE_ONLY = {4: ["exact-async", "exact-thread"]}  # caught by a wrong value with exit 0, not by an error


def check(nift, only=""):
    failing = []
    names = [n for n in CASES if only in n]
    for name in names:
        problems = PROBLEMS[name] = CASES[name](nift)
        print(("PASS " if not problems else "FAIL ") + name)
        for p in problems:
            print("    " + p)
        if problems:
            failing.append(name)
    print("async-memory: %d/%d pass" % (len(names) - len(failing), len(names)))
    return failing


def mutants():
    bad = 0
    for n, expected in sorted(MUTANTS.items()):
        out = os.path.join(ROOT, ".build", "mutant-nift", "async-memory-%d" % n)
        try:
            b = subprocess.run([sys.executable, "-B", os.path.join(HERE, "sanctioned_mutant.py"), "--out", out,
                                "--define=-DNIFT_ASYNC_MEMORY_MUTANT=%d" % n], capture_output=True, text=True)
            if b.returncode != 0:
                print("FAIL async-memory mutant %d: harness: build failed: %s" % (n, b.stderr[-400:] or b.stdout[-400:]))
                bad += 1
                continue
            HOOK["binary"] = os.path.join(out, "nift") if n in (9, 11) or n >= 13 else None  # these keep the hook
            failing = []
            for name in expected:
                failing += check(os.path.join(out, "nift"), name)
            missed = [c for c in expected if c not in failing]
            missed += [c + " (by value)" for c in VALUE_ONLY.get(n, [])
                       if not PROBLEMS.get(c) or not PROBLEMS[c][0].startswith("exit 0 with another value")]
            if missed:
                print("FAIL async-memory mutant %d: not caught by %s" % (n, missed))
                bad += 1
            else:
                print("PASS async-memory mutant %d: caught by %s" % (n, ", ".join(expected)))
        finally:
            shutil.rmtree(out, ignore_errors=True)
    print("%s async-memory mutants: %d of %d caught" % ("FAIL" if bad else "PASS", len(MUTANTS) - bad, len(MUTANTS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift")
    ap.add_argument("--only", default="")
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()
    if args.mutants:
        return mutants()
    out, error = build_hook(12)  # the fault-injection build of these sources (test-only; deleted afterwards)
    try:
        if error:
            print("FAIL async-memory: harness: fault-injection build failed: %s" % error)
            return 1
        HOOK["binary"] = os.path.join(out, "nift")
        return 1 if check(os.path.abspath(args.nift), args.only) else 0
    finally:
        shutil.rmtree(out, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
