#!/usr/bin/env python3
"""Reference semantics, Phase A (D-NEXT-refsem): maps, sets, stacks and struct
instances are identity-bearing references on the VM.

  refsem_a.py --nift ./nift            # the suite (make test-runtime2-refsem)
  refsem_a.py --nift ./nift --update   # rewrite the approved/*.vm, *.legacy and equiv/*.vm goldens

Contract: docs/runtime-2/checkpoints/weekend-refsem-a.md.

  parity/*.f    legacy (NIFT_VM=0) and the VM (NIFT_VM=require) give the same
                stdout, stderr and exit status, and the VM ran the program
                (legacy's collections and structs are references too).
  approved/*.f  approved differences (D-NEXT-refsem): the VM output is NAME.vm,
                the legacy witness NAME.legacy, and the two differ.
  refuse/*.f    refused before any effect: exit 1, no stdout (every fixture
                prints first), "no part of it ran", the reason in NAME.reason.
  equiv/*.f     cooperative (NIFT_VM_COOPERATIVE_THREADS=1) and parallel task
                execution give the same transcript, NAME.vm, both on the VM (D7).
  heap          a refused cycle leaks nothing (4 and 40 refused attempts end
                with the same live heap), and copies, loop snapshots and the
                cooperative spawn copy stop at the heap budget before building.

Exit 0 when every check passes. A mutant binary must make it fail (refsem/mutants.sh).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "refsem")


def run(nift, path, mode, work, budget=None, extra=None):
    d = tempfile.mkdtemp(dir=work)
    name = os.path.basename(path)
    shutil.copy(path, os.path.join(d, name))
    stats = os.path.join(d, "stats.jsonl")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C",
           "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": mode, "NIFT_VM_STATS": stats}
    if budget is not None:
        env["NIFT_VM_HEAP_BUDGET"] = str(budget)
    if extra:
        env.update(extra)
    p = subprocess.run(["timeout", "120", nift, name], cwd=d, env=env, stdin=subprocess.DEVNULL,
                       capture_output=True)
    st = None
    if os.path.exists(stats):
        try:
            st = json.loads(open(stats).read().splitlines()[-1])
        except (OSError, IndexError, ValueError):
            st = None
    norm = lambda b: b.decode("utf-8", "replace").replace(d + os.sep, "")
    return norm(p.stdout), norm(p.stderr), p.returncode, st


def transcript(out, err, rc):
    return out + "--- stderr\n" + err + "--- exit %d\n" % rc


def fixtures(kind):
    d = os.path.join(HERE, kind)
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".f"))


def ran_on_vm(st):
    return st is not None and st.get("outcome") in ("vm", "vm-error") and st.get("legacy_executions", 1) == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = []
    count = 0

    def check(ok, what):
        nonlocal count
        count += 1
        if not ok:
            failures.append(what)
            print("FAIL " + what)

    with tempfile.TemporaryDirectory(prefix="refsem-") as work:
        for f in fixtures("parity"):
            name = os.path.basename(f)
            lo, le, lr, _ = run(nift, f, "0", work)
            vo, ve, vr, st = run(nift, f, "require", work)
            check(ran_on_vm(st), "parity %s: not run by the VM: %s" % (name, ve.strip()[:200]))
            check((lo, le, lr) == (vo, ve, vr),
                  "parity %s: legacy %r/%r/%d vs VM %r/%r/%d" % (name, lo[-300:], le[-200:], lr, vo[-300:],
                                                                  ve[-200:], vr))
        for f in fixtures("approved"):
            name = os.path.basename(f)
            base = f[:-2]
            vo, ve, vr, st = run(nift, f, "require", work)
            lo, le, lr, _ = run(nift, f, "0", work)
            vm_t, legacy_t = transcript(vo, ve, vr), transcript(lo, le, lr)
            if args.update:
                open(base + ".vm", "w").write(vm_t)
                open(base + ".legacy", "w").write(legacy_t)
                continue
            check(ran_on_vm(st), "approved %s: not run by the VM" % name)
            want_vm = open(base + ".vm").read() if os.path.exists(base + ".vm") else None
            want_legacy = open(base + ".legacy").read() if os.path.exists(base + ".legacy") else None
            check(vm_t == want_vm, "approved %s: VM output\n%s\nexpected\n%s" % (name, vm_t, want_vm))
            check(legacy_t == want_legacy, "approved %s: legacy witness changed\n%s" % (name, legacy_t))
            check(vm_t != legacy_t, "approved %s: no longer differs from legacy" % name)
        for f in fixtures("refuse"):
            name = os.path.basename(f)
            reason = open(f[:-2] + ".reason").read().strip()
            vo, ve, vr, st = run(nift, f, "require", work)
            check(vr == 1 and vo == "", "refuse %s: exit %d stdout %r (must be 1 and empty)" % (name, vr, vo))
            check("(%s)" % reason in ve and "no part of it ran" in ve,
                  "refuse %s: expected reason %s before effects, got %r" % (name, reason, ve.strip()[:300]))
        for f in fixtures("equiv"):
            name = os.path.basename(f)
            base = f[:-2]
            po, pe, pr, pst = run(nift, f, "require", work)
            co, ce, cr, cst = run(nift, f, "require", work, extra={"NIFT_VM_COOPERATIVE_THREADS": "1"})
            par, coop = transcript(po, pe, pr), transcript(co, ce, cr)
            if args.update:
                open(base + ".vm", "w").write(par)
                continue
            check(ran_on_vm(pst) and ran_on_vm(cst), "equiv %s: not run by the VM in both schedulers" % name)
            check(par == coop, "equiv %s: parallel\n%s\ncooperative\n%s" % (name, par, coop))
            want = open(base + ".vm").read() if os.path.exists(base + ".vm") else None
            check(par == want, "equiv %s: output\n%s\nexpected\n%s" % (name, par, want))
        if args.update:
            print("updated goldens")
            return 0
        heap = os.path.join(HERE, "heap")
        # A refused cycle leaves nothing behind: ten times the refused attempts
        # end with the same live heap (a leaked cycle would accumulate).
        a = run(nift, os.path.join(heap, "cycle_refused.f"), "require", work)
        b = run(nift, os.path.join(heap, "cycle_refused_many.f"), "require", work)
        la = a[3].get("heap_live") if a[3] else None
        lb = b[3].get("heap_live") if b[3] else None
        check(a[2] == 0 and b[2] == 0 and a[0] == "8\n" and b[0] == "80\n" and la is not None and la == lb,
              "heap cycle_refused: exit %d/%d %r/%r, live heap %s != %s (x10): %r" % (a[2], b[2], a[0], b[0], la, lb,
                                                                                     a[1][:200]))
        for fname, budget in (("budget_deepcopy.f", 4_750_000), ("budget_copy.f", 4_750_000),
                              ("budget_snapshot.f", 4_750_000), ("budget_spawn.f", 4_750_000)):
            vo, ve, vr, st = run(nift, os.path.join(heap, fname), "require", work, budget=budget,
                                 extra={"NIFT_VM_COOPERATIVE_THREADS": "1"} if "spawn" in fname else None)
            check(vr == 70 and "memory budget exceeded" in ve and vo == "filled\n",
                  "heap %s: expected the budget stop after `filled` (exit 70), got exit %d %r %r" %
                  (fname, vr, vo, ve[:200]))
            vo, ve, vr, st = run(nift, os.path.join(heap, fname), "require", work,
                                 extra={"NIFT_VM_COOPERATIVE_THREADS": "1"} if "spawn" in fname else None)
            check(vr == 0 and vo.endswith("done\n"), "heap %s: unbounded run failed: %d %r" % (fname, vr, ve[:200]))
    print("%s refsem-a: %d checks, %d failure(s)" % ("PASS" if not failures else "FAIL", count, len(failures)))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
