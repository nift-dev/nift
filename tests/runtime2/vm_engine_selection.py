#!/usr/bin/env python3
"""Runtime 2.0 CP12: engine selection is final before execution (D-013).

  vm_engine_selection.py --nift ./nift

Checks the six CP12 invariants on programs with observable effects:

  A  single execution    every unit executes at most once: the statistics
                         report vm_executions + legacy_executions == 1, and
                         every effect (each printed line, each host operation
                         in the CP08 host trace, NIFT_TRACE_HOST) happens
                         exactly as often as under legacy;
  B  pre-execution selection  NIFT_VM=classify (no execution at all) already
                         names the engine NIFT_VM=1 then uses;
  C  no partial replay   a program that fails on the VM after producing
                         output is not re-run: its output appears once and
                         the VM reports the error (outcome vm-error);
  D  no duplicated effects  file writes (refused by the VM, so the unit runs
                         on legacy from the start) happen once: the appended
                         file and the trace show one write;
  E  explicit fallback   unsupported units are classified before execution
                         (outcome fallback, a reason, execute_us 0,
                         vm_executions 0) and run on legacy;
  F  VM errors remain errors  an unexpected VM failure (here: the memory
                         budget exceeded at a depth legacy also reaches) is
                         reported with exit status 70 and never run on legacy.

Exit 1 on any failure.
"""
import argparse
import collections
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vm_policies  # noqa: E402

FILE_WRITE = ('f := file("log.txt")\nf.open("a")\nf.write("line\\n")\nf.save()\nf.close()\n')

# (name, source, expected NIFT_VM=1 outcome, extra environment); a difference
# from legacy must be approved by the policy named in APPROVED.
CASES = [
    ("vm-prints", 'fn(f(x)) { print("f" + x)\nreturn x * 2 }\nprint("start")\ni := 0\n'
                  'while (i < 5) { print("loop " + i)\ns := f(i)\ni += 1 }\nprint(f(9))\n', "vm", {}),
    ("vm-error-after-output", 'print("one")\ni := 0\nwhile (i < 3) { print(i)\ni += 1 }\nprint(10 / (i - 3))\n'
                              'print("never")\n', "vm-error", {}),
    ("vm-error-in-function", 'fn(g(a, k)) { print("g " + k)\nreturn a[k] }\na := [1, 2]\nk := 0\n'
                             'while (k < 4) { print(g(a, k))\nk += 1 }\n', "vm-error", {}),
    ("vm-overflow-in-loop", 'x := 3\ni := 0\nwhile (i < 80) { print(x)\nx = x * 2\ni += 1 }\n', "vm-error", {}),
    # D-018a: legacy re-evaluates a loop statement whose value reaches 2^53 on
    # its textual path, calls included (the call prints twice); the VM
    # evaluates it once (approved difference, vm_policies.py).
    ("vm-large-reevaluation", 'fn(loud(x)) { print("loud " + x)\nreturn x }\nw := 9007199254740991\n'
                              'i := 0\nwhile (i < 2) { t := loud(w + i) + 2\nprint(t)\ni += 1 }\n', "vm", {}),
    ("vm-large-output-then-error", 'i := 0\nwhile (i < 200000) { print("line " + i)\ni += 1 }\nprint(1 % 0)\n',
     "vm-error", {}),
    ("vm-recursion-budget", 'fn(d(n)) { if (n == 0) { return 0 }\nreturn d(n - 1) + 1 }\nprint("s")\n'
                            'print(d(100005))\n', "vm-error", {}),
    ("fallback-file-write", FILE_WRITE + 'print("wrote")\ni := 0\nwhile (i < 3) { print(i)\ni += 1 }\n',
     "fallback", {}),
    ("fallback-file-write-then-error", FILE_WRITE + 'print("wrote")\nprint(1 / 0)\n', "fallback", {}),
    ("fallback-type-error", 'print("a")\nprint("x" < 1)\n', "fallback", {}),
    ("fallback-aliasing", 'a := [1]\nb := a\nb.push(2)\nprint(a.length())\n', "fallback", {}),
    ("internal-failure", 'print("start")\ns := "abcdefghijklmnopq"\nk := 0\nwhile (k < 22) { s += s\nk += 1 }\n'
                         'print(s.length())\n', "internal-error", {"NIFT_VM_HEAP_BUDGET": str(4 << 20)}),
]


APPROVED = {
    "vm-recursion-budget": "D-001#4 recursion",
}
# Approved differences no policy can verify (review round 2): the VM's whole
# result is pinned, derived by hand / with Python ints, never from the VM.
#   vm-error-in-function: g(a, 2) indexes [1, 2] at 2 (legacy's loop wording
#     "invalid index" at the call site omits the index): canonical message at
#     `a[k]`, line 2 column 8.
#   vm-large-reevaluation: t = (w + i) + 2 exactly, loud printed once per
#     iteration (legacy re-evaluates and prints it twice).
W = 9007199254740991
GOLDEN = {
    "vm-error-in-function": (b"g 0\n1\ng 1\n2\ng 2\n",
                             b"error: {path}:2:8: JSON array index 2 is out of range in 'a[k]'\n", 1),
    "vm-large-reevaluation": (("loud %d\n%d\nloud %d\n%d\n" % (W, W + 2, W + 1, W + 3)).encode(), b"", 0),
}
# Lines the VM must print exactly once per iteration where legacy printed them
# twice (exactly-once evaluation).
ONCE = {"vm-large-reevaluation": ("loud 9007199254740991", "loud 9007199254740992")}


def run(nift, d, mode, extra):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "HOME": d, "TMPDIR": d}
    for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
        if k in os.environ:
            env[k] = os.environ[k]
    env.update(extra)
    stats = os.path.join(d, ".stats")
    if mode:
        env.update({"NIFT_VM": mode, "NIFT_VM_STATS": stats})
    for leftover in ("log.txt", stats, ".trace"):
        if os.path.exists(os.path.join(d, leftover)):
            os.unlink(os.path.join(d, leftover))
    with open(os.path.join(d, ".trace"), "w") as trace:
        env["NIFT_TRACE_HOST"] = str(trace.fileno())
        p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL,
                           pass_fds=(trace.fileno(),), timeout=120)
    ops = collections.Counter()
    for line in open(os.path.join(d, ".trace")):
        rec = json.loads(line)
        ops[(rec.get("op"), os.path.basename(str(rec.get("arg", ""))))] += 1
    log = open(os.path.join(d, "log.txt")).read() if os.path.exists(os.path.join(d, "log.txt")) else None
    st = None
    if mode and os.path.exists(stats):
        lines = open(stats).read().splitlines()
        st = json.loads(lines[-1]) if lines else None
    return (p.stdout, p.stderr, p.returncode), ops, log, st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    work = tempfile.mkdtemp(prefix="nift-vm-engine-")
    failures = 0

    def fail(name, why):
        nonlocal failures
        failures += 1
        print(f"FAIL {name}: {why}")

    try:
        for name, source, expect, extra in CASES:
            d = os.path.join(work, name)
            os.makedirs(d)
            with open(os.path.join(d, "p.f"), "w") as f:
                f.write(source)
            legacy, legacy_ops, legacy_log, _ = run(nift, d, None, extra)
            classify, _, classify_log, cst = run(nift, d, "classify", extra)
            vm, vm_ops, vm_log, st = run(nift, d, "1", extra)
            if not st or not cst:
                fail(name, "no statistics")
                continue
            # A: one engine, once.
            if st.get("vm_executions", 0) + st.get("legacy_executions", 0) != 1 or st["outcome"] == "replayed":
                fail(name, f"executions vm={st.get('vm_executions')} legacy={st.get('legacy_executions')}")
            if st["outcome"] != expect:
                fail(name, f"outcome {st['outcome']}, expected {expect}")
            # B: classification (no execution) selects the same engine.
            selected = "vm" if cst["outcome"] == "compiled" else cst["outcome"]
            if (selected == "vm") != (st["outcome"] in ("vm", "vm-error", "internal-error")):
                fail(name, f"classify said {cst['outcome']}, NIFT_VM=1 ran {st['outcome']}")
            if classify[0] or classify_log is not None or cst.get("execute_us", 0) != 0:
                fail(name, "classify executed something")
            # E: fallback before execution.
            if st["outcome"] == "fallback":
                if st.get("vm_executions") or st.get("execute_us", 0) != 0 or not st.get("reason"):
                    fail(name, "fallback is not a pre-execution selection with a reason")
            # F: an internal VM failure is reported, never run on legacy.
            if expect == "internal-error":
                if vm[2] != 70 or b"NIFT_VM internal failure" not in vm[1] or st.get("legacy_executions"):
                    fail(name, f"internal failure not reported as such (exit {vm[2]})")
                if vm[0] != b"start\n":
                    fail(name, "output after an internal failure differs (legacy must not have run)")
                continue
            # A, C, D: identical observable behaviour, every effect once.
            if name in GOLDEN:
                out, err, code = GOLDEN[name]
                expected = (out, err.replace(b"{path}", os.path.join(d, "p.f").encode()), code)
                if vm != expected:
                    fail(name, f"VM result {vm!r} differs from the pinned expectation {expected!r}")
            elif vm != legacy:
                verdict, policy = vm_policies.classify(legacy, vm, st)
                if verdict != "approved" or policy != APPROVED.get(name):
                    fail(name, f"differs from legacy ({verdict} {policy}): exit {legacy[2]}/{vm[2]} "
                               f"stderr {legacy[1][:120]!r} / {vm[1][:120]!r}")
            elif name in APPROVED:
                fail(name, f"expected the approved difference {APPROVED[name]}, got identical behaviour")
            for line in ONCE.get(name, ()):
                n_vm, n_legacy = vm[0].decode().splitlines().count(line), legacy[0].decode().splitlines().count(line)
                if n_vm != 1 or n_legacy != 2:
                    fail(name, f"{line!r}: VM {n_vm} times (want 1), legacy {n_legacy} (want 2)")
            if vm_ops != legacy_ops:
                fail(name, f"host operations differ: legacy {dict(legacy_ops)} vm {dict(vm_ops)}")
            if FILE_WRITE in source and (vm_ops[("save", "log.txt")] != 1 or legacy_ops[("save", "log.txt")] != 1):
                fail(name, f"the traced write did not happen exactly once: {dict(vm_ops)}")
            if vm_log != legacy_log or (legacy_log is not None and legacy_log != "line\n"):
                fail(name, f"file effect differs or duplicated: legacy {legacy_log!r} vm {vm_log!r}")
            lines = vm[0].decode().splitlines()
            dup = [l for l, n in collections.Counter(lines).items() if n > 1 and not l.startswith("loud")]
            if dup:
                fail(name, f"duplicated output lines {dup[:3]}")
            print(f"ok {name}: {st['outcome']} {st.get('reason', '')[:60]}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(("PASS" if not failures else "FAIL") + f" vm engine selection: {len(CASES)} cases, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
