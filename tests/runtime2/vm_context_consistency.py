#!/usr/bin/env python3
"""Runtime 2.0 CP12 (D-018): a function means the same in every context.

  vm_context_consistency.py --nift ./nift

Each function body below is called with each argument from five contexts:
directly at the top level, inside a loop, from another function at the top
level, from another function inside a loop, and in a loop condition. Every
function is declared before the calls, so a correct engine gives identical
stdout, stderr (error message and source position) and exit status in all
five contexts. For each (body, argument) the VM runs (NIFT_VM=1) must agree
with each other in every context where the unit ran on the VM; the contexts
where it was refused before execution are counted (refusal is allowed to
depend on the context: it selects legacy for the whole unit). Legacy's own
context-dependence (the reason for D-018) is reported for comparison.

The suite fails if two VM-run contexts differ, if any run has an internal
error or is not executed exactly once, or if fewer than MIN_ALL_CONTEXTS
(body, argument) pairs ran on the VM in all five contexts.
"""
import argparse
import collections
import concurrent.futures as cf
import json
import os
import shutil
import subprocess
import sys
import tempfile

HELPERS = "fn(f2(a)) { return a * 3 }\n"
BODIES = {
    "add": "return p + 1",
    "square": "return p * p",
    "index": "q := [1, 2]\nreturn q[p]",
    "if": 'if (p > 1) { return "big" }\nreturn "small"',
    "assign": "x := 1\nx = p\nreturn x",
    "divide-zero": "return p / 0",
    "member": 'o := {"k": p}\nreturn o.k',
    "append": 's := "a"\ns += p\nreturn s',
    "length": "return p.length()",
    "negate": "return -p",
    "modulo": "n := p\nn %= 2\nreturn n",
    "helper": "return f2(p) + 1",
    "large": "return (p + 9007199254740991) - 9007199254740991",
    "loop": "t := 0\nwhile (t < 3 && t > -10) { t += p }\nreturn t",
    "array": "return [p, p]",
    "recursion": "if (p <= 0) { return 0 }\nreturn f(p - 1) + 1",
    "print": 'print("in f " + p)\nreturn p',
    "nested-if-error": "if (p > 0) { if (p < 100) { return [1][p + 4] } }\nreturn 0",
}
ARGS = ["1", "3", "2.5", '"s"', "[1, 2]", "9007199254740991", "-4"]
CONTEXTS = {
    "direct": "print(f({arg}))\n",
    "loop": "i := 0\nwhile (i < 1) {{ i += 1\nprint(f({arg})) }}\n",
    "nested": "print(g({arg}))\n",
    "nested-loop": "i := 0\nwhile (i < 1) {{ i += 1\nprint(g({arg})) }}\n",
    "condition": 'r := 0\nwhile (r == 0 && f({arg}) != "never") {{ r = 1 }}\nprint(r)\n',
}
MIN_ALL_CONTEXTS = 25


def program(body, arg, context):
    fn = "fn(f(p)) {\n" + body + "\n}\n"
    return HELPERS + fn + "fn(g(p)) { return f(p) }\n" + CONTEXTS[context].format(arg=arg)


def run(nift, d, source, mode):
    path = os.path.join(d, "p.f")
    with open(path, "w") as f:
        f.write(source)
    stats = os.path.join(d, ".stats")
    if os.path.exists(stats):
        os.unlink(stats)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "HOME": d, "TMPDIR": d}
    for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
        if k in os.environ:
            env[k] = os.environ[k]
    if mode:
        env.update({"NIFT_VM": mode, "NIFT_VM_STATS": stats})
    p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL, timeout=60)
    st = None
    if mode and os.path.exists(stats):
        lines = open(stats).read().splitlines()
        st = json.loads(lines[-1]) if lines else None
    return (p.stdout, p.stderr, p.returncode), st


def check(nift, work, body_name, arg):
    d = tempfile.mkdtemp(dir=work)
    try:
        vm, legacy, problems = {}, {}, []
        for context in CONTEXTS:
            source = program(BODIES[body_name], arg, context)
            res, st = run(nift, d, source, "1")
            leg, _ = run(nift, d, source, None)
            legacy[context] = leg
            if not st or st.get("vm_executions", 0) + st.get("legacy_executions", 0) != 1:
                problems.append(f"{context}: not executed exactly once")
                continue
            if st["outcome"] == "internal-error":
                problems.append(f"{context}: internal error")
                continue
            if st["outcome"] in ("vm", "vm-error"):
                vm[context] = res
        return body_name, arg, vm, legacy, problems
    finally:
        shutil.rmtree(d, ignore_errors=True)


def f_lines(result):
    """What f itself printed."""
    return [l for l in result[0].split(b"\n") if l.startswith(b"in f ")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--jobs", type=int, default=4)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    work = tempfile.mkdtemp(prefix="nift-vm-context-")
    failures = []
    all_contexts = 0
    ran = collections.Counter()
    legacy_inconsistent = 0
    try:
        jobs = [(b, a) for b in BODIES for a in ARGS]
        with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for body_name, arg, vm, legacy, problems in pool.map(lambda j: check(nift, work, *j), jobs):
                label = f"{body_name}({arg})"
                failures += [f"{label} {p}" for p in problems]
                for context in vm:
                    ran[context] += 1
                if len(vm) == len(CONTEXTS):
                    all_contexts += 1
                # The direct, loop, nested and nested-loop contexts print the
                # same thing; the condition context only f's own output and
                # the error.
                full = {c: r for c, r in vm.items() if c != "condition"}
                if len(set(full.values())) > 1:
                    failures.append(f"{label}: VM results differ by context: " +
                                    "; ".join(f"{c}: exit {r[2]} out {r[0][-80:]!r} err {r[1][-120:]!r}"
                                              for c, r in sorted(full.items())))
                # Every context: f's own output, the error (message and
                # position) and the exit status.
                core = {(tuple(f_lines(r)), r[1], r[2]) for r in vm.values()}
                if len(core) > 1:
                    failures.append(f"{label}: VM output of f, errors or exit differ by context: {sorted(core)}")
                lfull = {c: r for c, r in legacy.items() if c != "condition"}
                if len(set(lfull.values())) > 1:
                    legacy_inconsistent += 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    total = len(BODIES) * len(ARGS)
    if all_contexts < MIN_ALL_CONTEXTS:
        failures.append(f"only {all_contexts} (body, argument) pairs ran on the VM in all contexts "
                        f"(minimum {MIN_ALL_CONTEXTS})")
    for f in failures[:30]:
        print("FAIL " + f)
    print(f"context consistency: {total} (body, argument) pairs x {len(CONTEXTS)} contexts; ran on the VM in all "
          f"contexts {all_contexts}; VM runs per context {dict(ran)}; legacy context-dependent on "
          f"{legacy_inconsistent} pairs")
    print(("PASS" if not failures else "FAIL") + f" vm context consistency: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
