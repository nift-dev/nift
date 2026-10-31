#!/usr/bin/env python3
"""Coroutines and async functions in programs that import modules (weekend
candidate `coro-module`; contract: docs/runtime-2/checkpoints/weekend-coro-module.md).

  coro_module.py --nift ./nift [--only NAME] [--list]

Every case is a small program written to a fresh directory (main.f and its
modules). Kinds:

  parity   the VM (NIFT_VM unset: required, the default) runs the linked unit
           exactly once (NIFT_VM_STATS: outcome vm/vm-error, vm_executions 1,
           legacy_executions 0) with legacy's stdout, stderr and exit status
           (NIFT_VM=0), with 1 and 4 parallel workers.
  inline   as parity, except stdout/stderr/exit are compared with the same
           program with every module pasted in at its import, on the VM (the
           linked unit is one program: modules add nothing). Used where legacy
           and the VM already differ for the inline program (a recorded CP23
           difference, named in the case).
  refuse   refused before any effect: exit 1, empty stdout (each entry starts
           with print("pre")), outcome require-error, vm_executions 0,
           legacy_executions 0, the exact reason id, and the exact
           file:line:column of the refused node (a module's own position for
           code in a module). Legacy's own result is recorded (`legacy`).

Also: the cooperative model keeps CP22's whole-unit `coro-module`;
NIFT_VM=classify reports the same reason as a run; a piped session refuses an
importing coroutine unit as `session:coroutine` first; NIFT_VM=1 still falls
back for a refused unit.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

FAILS = []

CLEAN = {"clean.f": "fn(target(x)) { return x * 2 }\nexport(target)\n"}

# Nested imports: main -> a -> b -> c. c and b are stateless; the state is
# only ever read by the importer's own top-level code.
NESTED = {
    "a.f": 'import("b.f")\nfn(a_twice(x)) { return b_add(x, x) }\nexport(a_twice)\n',
    "b.f": 'import("c.f")\nfn(b_add(x, y)) { return c_id(x) + c_id(y) }\nexport(b_add)\n',
    "c.f": 'fn(c_id(x)) { return x }\nexport(c_id)\n',
}

COUNTER = {"counter.f": "count := 0\nfn(bump()) { count += 1; return count }\nfn(peek()) { return count }\n"
                        "export(bump)\nexport(peek)\n"}

CASES = [
    # ---- admitted: parity with legacy -------------------------------------
    ("clean-module-worker", "parity", {**CLEAN, "main.f":
        'import("clean.f")\nw := thread(target, 2)\nprint(w.join())\n'}, {}),
    ("clean-wrappers", "parity", {**CLEAN, "main.f":
        'import("clean.f")\nfn(wrap()) { return target(3) }\nfn[async](awrap()) { return target(4) }\n'
        'w := thread(wrap)\nf := awrap()\nprint(w.join())\nprint(await f)\n'}, {}),
    ("async-module-function", "parity", {"lib.f":
        'fn[async](twice(x)) { return x * 2 }\nfn(plain(x)) { return x + 1 }\nexport(twice)\nexport(plain)\n',
        "main.f": 'import("lib.f")\nf := twice(21)\nprint(await f)\nprint(plain(1))\n'}, {}),
    ("unrelated-module-state", "parity", {"m.f": "mval := 1\nexport(mval)\n", "main.f":
        'import("m.f")\nfn[async](f(a)) { return a }\nprint(await f(mval))\n'}, {}),
    ("module-spawns-after-init", "parity", {"par.f":
        'fn(sq(x)) { return x * x }\n'
        'fn(sum_sq(a, b)) { t := thread(sq, a); u := thread(sq, b); return t.join() + u.join() }\n'
        'export(sum_sq)\n', "main.f": 'import("par.f")\nprint(sum_sq(3, 4))\n'}, {}),
    ("nested-imports", "parity", {**NESTED, "main.f":
        'import("a.f")\nfn[async](go(x)) { return a_twice(x) }\n'
        'w := thread(a_twice, 5)\nf := go(7)\nprint(w.join())\nprint(await f)\n'}, {}),
    ("nested-imports-task-in-module", "parity", {
        "a.f": 'import("b.f")\nfn(a_par(x)) { t := thread(b_sq, x); return t.join() + 1 }\nexport(a_par)\n',
        "b.f": 'fn(b_sq(x)) { return x * x }\nexport(b_sq)\n',
        "main.f": 'import("a.f")\nfn[async](go()) { return a_par(6) }\nprint(await go())\n'}, {}),
    # CP22's original concern (initialisation order vs spawns): tasks started
    # before an import keep running while the module initialises, and are
    # joined after it; the module's functions then run in new tasks.
    ("task-across-import", "parity", {**CLEAN, "main.f":
        'fn(slow(x)) { sleep(100); return x + 1 }\nfn[async](aslow(x)) { sleep(50); return x + 2 }\n'
        't := thread(slow, 1)\nf := aslow(1)\nimport("clean.f")\nprint(target(t.join()))\nprint(target(await f))\n'
        'u := thread(target, 10)\nprint(u.join())\n'}, {}),
    # Shared globals: the importer's own globals are snapshotted at the spawn
    # (legacy: every visible variable cloned); a module's exported value is
    # read by the importer and passed by value.
    ("shared-globals-importer", "parity", {"m.f":
        'base := 40\nfn(add(x, y)) { return x + y }\nexport(add)\nexport(base)\n', "main.f":
        'import("m.f")\ng := 1\nfn(work(x)) { return add(x, g) }\nfn[async](awork(x)) { return add(x, g) }\n'
        'local := base\nt := thread(work, base)\nf := awork(local)\ng = 100\n'
        'print(t.join())\nprint(await f)\nprint(g)\nprint(base)\n'}, {}),
    ("shared-globals-task-writes", "parity", {**CLEAN, "main.f":
        'import("clean.f")\ntotal := 5\nfn(work()) { total = total + target(1); return total }\n'
        't := thread(work)\nprint(t.join())\nprint(total)\n'}, {}),
    # Cancellation (CP23 cooperative cancellation; `cancel` is Runtime 2.0
    # only: legacy has no such callable, so these are `inline` cases with a
    # pinned result): a task running module code is cancelled before it
    # finishes; a finished task keeps its value.
    ("cancel-async-module-code", "inline", {"slow.f":
        'fn(slow_value()) { sleep(300); return 1 }\nfn[async](slow_task()) { sleep(300); return 2 }\n'
        'export(slow_value)\nexport(slow_task)\n', "main.f":
        'import("slow.f")\nfn[async](go()) { return slow_value() }\n'
        'p := go()\ncancel(p)\ntry { print(await p) } catch(e) { print(e.code) }\n'
        'q := slow_task()\ncancel(q)\ntry { print(await q) } catch(e) { print(e.code) }\nprint(p.done())\n'},
     {"stdout": "runtime.cancelled\nruntime.cancelled\ntrue\n", "exit": 0}),
    ("cancel-thread-module-code", "inline", {"slow.f":
        'fn(slow_value()) { sleep(300); return 1 }\nexport(slow_value)\n', "main.f":
        'import("slow.f")\nt := thread(slow_value)\ncancel(t)\n'
        'try { print(t.join()) } catch(e) { print(e.code) }\nprint(t.done())\n'},
     {"stdout": "runtime.cancelled\ntrue\n", "exit": 0}),
    ("cancel-after-completion", "inline", {**CLEAN, "main.f":
        'import("clean.f")\nfn[async](go()) { return target(5) }\np := go()\nprint(await p)\ncancel(p)\n'
        'print(await p)\nprint(p.done())\n'}, {"stdout": "10\n10\ntrue\n", "exit": 0}),
    # Exceptions crossing the module/coroutine boundary.
    ("error-module-to-await-caught", "parity", {"fail.f":
        'fn(boom(x)) { throw error("bad " + x, "user.x") }\nexport(boom)\n', "main.f":
        'import("fail.f")\nfn[async](go()) { return boom("a") }\n'
        'try { print(await go()) } catch(e) { print(e.code); print(e.message) }\n'
        't := thread(boom, "b")\ntry { print(t.join()) } catch(e) { print(e.code); print(e.message) }\n'}, {}),
    ("error-task-to-module-caught", "parity", {"guard.f":
        'fn(risky(x)) { if (x > 1) { throw error("too big", "user.big") }; return x }\n'
        'fn(guarded(x)) { t := thread(risky, x); try { return t.join() } catch(e) { return e.code } }\n'
        'export(guarded)\n', "main.f":
        'import("guard.f")\nfn[async](go(x)) { return guarded(x) }\nprint(await go(1))\nprint(await go(5))\n'}, {}),
    ("error-async-module-function-caught", "parity", {"afail.f":
        'fn[async](afail()) { throw error("async bad", "user.async") }\nexport(afail)\n', "main.f":
        'import("afail.f")\nf := afail()\ntry { await f } catch(e) { print(e.code); print(e.message) }\n'}, {}),
    ("error-uncaught-await-from-module", "parity", {"fail.f":
        'fn(boom()) { throw error("deep", "user.deep") }\nexport(boom)\n', "main.f":
        'import("fail.f")\nfn[async](go()) { return boom() }\nprint("before")\nprint(await go())\n'
        'print("after")\n'}, {}),
    # An uncaught Error from a joined thread: legacy reports the bare message,
    # the VM `thread: ` + message (CP23 thread-reporting contract; D-060 for
    # Host errors). The same difference exists without modules (`inline`).
    ("error-uncaught-join-from-module", "inline", {"fail.f":
        'fn(boom()) { throw error("deep", "user.deep") }\nexport(boom)\n', "main.f":
        'import("fail.f")\nprint("before")\nt := thread(boom)\nprint(t.join())\nprint("after")\n'},
     {"stdout": "before\n", "exit": 1, "at": "fail.f:1:14", "message": "thread: deep"}),

    # ---- refused: coro-module-init-task ----------------------------------
    ("init-thread", "refuse", {"spawner.f":
        'fn(work(x)) { return x * 2 }\nw := thread(work, 21)\nfn(result()) { return w.join() }\nexport(result)\n',
        "main.f": 'print("pre")\nimport("spawner.f")\nprint(result())\n'},
     {"reason": "coro-module-init-task", "at": "spawner.f:2:6", "legacy": "pre / 42, exit 0"}),
    ("init-async-call", "refuse", {"ainit.f":
        '@fn[async](val()) { return 7 }\nfut := val()\nfn(get2()) { return await fut }\nexport(get2)\n',
        "main.f": 'print("pre")\nimport("ainit.f")\nprint(get2())\n'},
     {"reason": "coro-module-init-task", "at": "ainit.f:2:8", "legacy": "pre / 7, exit 0"}),
    ("init-transitive", "refuse", {"tinit.f":
        'fn(work()) { return 1 }\nfn(start()) { t := thread(work); return t.join() }\nfirst := start()\n'
        'fn(get()) { return first }\nexport(get)\n', "main.f": 'print("pre")\nimport("tinit.f")\nprint(get())\n'},
     {"reason": "coro-module-init-task", "at": "tinit.f:3:10", "legacy": "pre / 1, exit 0"}),
    ("init-transitive-other-module", "refuse", {**NESTED,
        "a.f": 'import("b.f")\nfn(a_twice(x)) { return b_add(x, x) }\nseed := b_add(1, 2)\nexport(a_twice)\n',
        "b.f": 'fn(b_one(x)) { return x }\nfn(b_add(x, y)) { t := thread(b_one, x); return t.join() + y }\n'
               'export(b_add)\n',
        "main.f": 'print("pre")\nimport("a.f")\nprint(a_twice(2))\n'},
     {"reason": "coro-module-init-task", "at": "a.f:3:9", "legacy": "pre / 4, exit 0"}),
    ("init-nested-deepest", "refuse", {**NESTED,
        "c.f": 'fn(c_id(x)) { return x }\nfn[async](c_task()) { return 0 }\nwarm := c_task()\nexport(c_id)\n',
        "main.f": 'print("pre")\nimport("a.f")\nprint(a_twice(2))\n'},
     {"reason": "coro-module-init-task", "at": "c.f:3:9", "legacy": "pre / 4, exit 0"}),
    ("init-through-value", "refuse", {"vinit.f":
        'fn(work(x)) { return x }\nstarter := (x) => thread(work, x)\nfn(get()) { return 1 }\n'
        'kick := starter(1)\nexport(get)\n', "main.f": 'print("pre")\nimport("vinit.f")\nprint(get())\n'},
     {"reason": "coro-module-init-task", "at": "vinit.f:4:9", "legacy": "pre / 1, exit 0"}),
    ("init-failing-worker", "refuse", {"failer.f":
        'fn(boom()) { throw error("bad", "user.x") }\nt := thread(boom)\nt.join()\n',
        "main.f": 'print("pre")\nimport("failer.f")\nprint("unreached")\n'},
     {"reason": "coro-module-init-task", "at": "failer.f:2:6",
      "legacy": "pre / import: failed import created worker resources: bad, exit 1"}),

    # ---- refused: coro-module-capture ------------------------------------
    ("capture-thread-state", "refuse", {**COUNTER, "main.f":
        'print("pre")\nimport("counter.f")\nprint(bump())\nt := thread(bump)\nprint(t.join())\n'},
     {"reason": "coro-module-capture", "at": "main.f:4:6", "legacy": "pre / 1 / 2, exit 0 (task gets a copy)"}),
    ("capture-async-state", "refuse", {**COUNTER, "main.f":
        'print("pre")\nimport("counter.f")\nfn[async](abump()) { return bump() }\nf := abump()\n'
        'print(await f)\n'},
     {"reason": "coro-module-capture", "at": "main.f:4:6", "legacy": "pre / 1, exit 0"}),
    ("capture-module-async-own-state", "refuse", {"ms.f":
        'hits := 0\nfn[async](hit()) { hits += 1; return hits }\nexport(hit)\n', "main.f":
        'print("pre")\nimport("ms.f")\nf := hit()\nprint(await f)\n'},
     {"reason": "coro-module-capture", "at": "main.f:3:6", "legacy": "pre / 1, exit 0"}),
    ("capture-nested-transitive", "refuse", {
        "a.f": 'import("b.f")\nfn(a_get()) { return b_get() + 1 }\nexport(a_get)\n',
        "b.f": 'level := 10\nfn(b_get()) { return level }\nexport(b_get)\n',
        "main.f": 'print("pre")\nimport("a.f")\nfn[async](go()) { return a_get() }\nprint(await go())\n'},
     {"reason": "coro-module-capture", "at": "main.f:4:13", "legacy": "pre / 11, exit 0"}),
    ("capture-function-argument", "refuse", {**COUNTER, "main.f":
        'print("pre")\nimport("counter.f")\nfn(ignore(x)) { return 1 }\nt := thread(ignore, peek)\n'
        'print(t.join())\n'},
     {"reason": "coro-module-capture", "at": "main.f:4:6", "legacy": "pre / 1, exit 0"}),
    ("capture-lambda-argument", "refuse", {**COUNTER, "main.f":
        'print("pre")\nimport("counter.f")\nfn(apply(g)) { return g() }\nt := thread(apply, () => peek())\n'
        'print(t.join())\n'},
     {"reason": "coro-module-capture", "at": "main.f:4:6", "legacy": "pre / 0, exit 0"}),
    ("capture-through-value-call", "refuse", {**COUNTER, "main.f":
        'print("pre")\nimport("counter.f")\nreader := peek\nfn(call_it(g)) { return g() }\n'
        't := thread(call_it, 0)\nprint(reader())\n'},
     {"reason": "coro-module-capture", "at": "main.f:5:6", "legacy": "pre / 0, exit 0"}),
    ("capture-shared-mutex", "refuse", {"sharedmod.f":
        'm := mutex(0)\nfn(incr()) { m.lock(); m.set(m.get() + 1); m.unlock(); return 0 }\n'
        'fn(total()) { m.lock(); v := m.get(); m.unlock(); return v }\nexport(incr)\nexport(total)\n',
        "main.f": 'print("pre")\nimport("sharedmod.f")\na := thread(incr)\nb := thread(incr)\na.join()\nb.join()\n'
                  'print(total())\n'},
     {"reason": "coro-module-capture", "at": "main.f:3:6", "legacy": "pre / 2, exit 0 (mutex shared)"}),
    # The task touches no module state, but legacy captures the module's
    # whole environment, and a module binding holds a timer (v46 timer units).
    ("capture-module-handle", "refuse", {"tmod.f":
        'unused := timer()\nfn(plain()) { return 1 }\nexport(plain)\n', "main.f":
        'print("pre")\nimport("tmod.f")\nt := thread(plain)\nprint(t.join())\n'},
     {"reason": "coro-module-capture", "at": "main.f:3:6",
      "legacy": "pre / thread: capture contains a non-transferable timer, exit 1"}),
]

# A deliberately unsupported construct appears only in the entry (sanity: the
# admitted path still reports ordinary refusals in linked units).
COOPERATIVE = ("cooperative-model", {**CLEAN, "main.f":
    'print("pre")\nimport("clean.f")\nw := thread(target, 2)\nprint(w.join())\n'})


def check(ok, label, detail=""):
    print(("PASS " if ok else "FAIL ") + label + ((": " + detail) if detail and not ok else ""))
    if not ok:
        FAILS.append(label)


def write(files, root):
    root.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        (root / name).write_text(text)


def run(nift, cwd, extra, args=("main.f",), stdin=None):
    stats = Path(cwd) / "stats.jsonl"
    if stats.exists():
        stats.unlink()
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(extra)
    env["NIFT_VM_STATS"] = str(stats)
    env["HOME"] = str(cwd)
    p = subprocess.run([nift, *args], cwd=cwd, env=env, input=stdin, capture_output=True, text=True, timeout=60)
    rows = [json.loads(l) for l in stats.read_text().splitlines() if l.strip()] if stats.exists() else []
    return p, rows


def inline(files):
    """The linked unit by hand: every import replaced by the module's text, exports dropped."""
    def expand(name, seen):
        out = []
        for line in files[name].splitlines():
            m = re.fullmatch(r'\s*import\("([^"]+)"\)\s*', line)
            if m:
                out.append(expand(m.group(1), seen))
            elif not re.fullmatch(r'\s*export\(\w+\)\s*', line):
                out.append(line)
        return "\n".join(out)
    return {"main.f": expand("main.f", set()) + "\n"}


def engine_ok(rows, label):
    ok = len(rows) == 1 and rows[0].get("outcome") in ("vm", "vm-error") and \
        rows[0].get("vm_executions") == 1 and rows[0].get("legacy_executions") == 0
    check(ok, label + ": runs once on the VM, never on legacy", json.dumps(rows))
    return ok


LOC = re.compile(r"^error: (.*?):(\d+):(\d+): (.*)$", re.M)


def located(stderr):
    """(file:line:column, message) of the first error line, or (None, stderr)."""
    m = LOC.search(stderr)
    return (f"{Path(m.group(1)).name}:{m.group(2)}:{m.group(3)}", m.group(4)) if m else (None, stderr)


def case_parity(nift, name, files, work, kind, expect):
    d = work / name
    write(files, d)
    if kind == "parity":
        want, _ = run(nift, d, {"NIFT_VM": "0"})
    else:
        di = work / (name + "-inline")
        write(inline(files), di)
        want, rows = run(nift, di, {})
        engine_ok(rows, name + " (inline reference)")
        check((want.stdout, want.returncode) == (expect["stdout"], expect["exit"]),
              name + " (inline reference): pinned result", repr((want.stdout, want.stderr, want.returncode)))
    for workers in ("1", "4"):
        got, rows = run(nift, d, {"NIFT_VM_PARALLEL_WORKERS": workers})
        label = f"{name} [{workers} workers]"
        engine_ok(rows, label)
        if kind == "parity":
            check((got.stdout, got.stderr, got.returncode) == (want.stdout, want.stderr, want.returncode),
                  f"{label}: stdout, stderr and exit equal legacy",
                  f"got {(got.stdout, got.stderr, got.returncode)!r} want {(want.stdout, want.stderr, want.returncode)!r}")
            continue
        # The inline program's positions are in its one file; the linked
        # unit's are in the module that holds the code (expect["at"]).
        where, message = located(got.stderr)
        check((got.stdout, got.returncode, message) == (want.stdout, want.returncode, located(want.stderr)[1]),
              f"{label}: stdout, exit and message equal the inline program on the VM",
              f"got {(got.stdout, got.stderr, got.returncode)!r} want {(want.stdout, want.stderr, want.returncode)!r}")
        if "at" in expect:
            check(where == expect["at"] and message == expect["message"],
                  f"{label}: error at the module position {expect['at']}", got.stderr)


DIAG = re.compile(r"^error: (.*?):(\d+):(\d+): not compiled for the VM \(([^)]+)\)", re.M)


def case_refuse(nift, name, files, work, expect):
    d = work / name
    write(files, d)
    got, rows = run(nift, d, {})
    m = DIAG.search(got.stderr)
    where = f"{Path(m.group(1)).name}:{m.group(2)}:{m.group(3)}" if m else None
    check(got.returncode == 1 and got.stdout == "", f"{name}: exit 1 and nothing ran (no output)",
          f"exit {got.returncode} stdout {got.stdout!r}")
    check(len(rows) == 1 and rows[0].get("outcome") == "require-error" and rows[0].get("vm_executions") == 0
          and rows[0].get("legacy_executions") == 0 and rows[0].get("reason") == expect["reason"],
          f"{name}: refused before execution as {expect['reason']}", json.dumps(rows))
    check(m is not None and m.group(4) == expect["reason"] and where == expect["at"],
          f"{name}: diagnostic at {expect['at']}", got.stderr.splitlines()[0] if got.stderr else "")
    check(rows and rows[0].get("positioned", True) is not False, f"{name}: position recorded", json.dumps(rows))
    cl, crow = run(nift, d, {"NIFT_VM": "classify"})
    check(crow and crow[0].get("reason") == expect["reason"], f"{name}: classify reports the same reason",
          json.dumps(crow))
    fb, frow = run(nift, d, {"NIFT_VM": "1"})
    lg, _ = run(nift, d, {"NIFT_VM": "0"})
    check(frow and frow[0].get("outcome") == "fallback" and frow[0].get("vm_executions") == 0 and
          (fb.stdout, fb.returncode) == (lg.stdout, lg.returncode),
          f"{name}: NIFT_VM=1 falls back to legacy's result", json.dumps(frow))
    print(f"     legacy: exit {lg.returncode}, stdout {lg.stdout!r} (recorded: {expect['legacy']})")


def extras(nift, work):
    name, files = COOPERATIVE
    d = work / name
    write(files, d)
    got, rows = run(nift, d, {"NIFT_VM_COOPERATIVE_THREADS": "1"})
    check(got.returncode == 1 and got.stdout == "" and rows and rows[0].get("reason") == "coro-module" and
          rows[0].get("vm_executions") == 0, "cooperative model: CP22's whole-unit coro-module refusal stays",
          json.dumps(rows))
    got, rows = run(nift, d, {})
    check(got.returncode == 0 and got.stdout == "pre\n4\n", "parallel default: the same unit runs",
          repr((got.stdout, got.stderr)))
    # Persistent sessions are out of scope: a piped session refuses an import
    # statement itself (statement:Import) and any coroutine unit
    # (session:coroutine); nothing of the refused unit runs, the session stops.
    for label, text, reason in [("import", 'import("clean.f")\nw := thread(target, 2)\nprint(w.join())\n',
                                 "statement:Import"),
                                ("coroutine", 'fn(two()) { return 2 }\nw := thread(two)\nprint(w.join())\n',
                                 "session:coroutine")]:
        got, rows = run(nift, d, {}, args=(), stdin=text)
        reasons = [r.get("reason") for r in rows]
        check(got.returncode == 1 and reason in reasons and "2" not in got.stdout.split() and
              "4" not in got.stdout.split(), f"piped session ({label}): refused as {reason}, nothing of it ran",
              repr((reasons, got.stdout, got.stderr)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--only")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.list:
        for name, kind, _, expect in CASES:
            print(kind, name, expect.get("reason", ""))
        return 0
    nift = os.path.abspath(a.nift)
    with tempfile.TemporaryDirectory(prefix="coro-module-") as td:
        work = Path(td)
        for name, kind, files, expect in CASES:
            if a.only and a.only != name:
                continue
            if kind == "refuse":
                case_refuse(nift, name, files, work, expect)
            else:
                case_parity(nift, name, files, work, kind, expect)
        if not a.only:
            extras(nift, work)
    print(("FAIL" if FAILS else "PASS") + f" coro-module: {len(FAILS)} failure(s)")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
