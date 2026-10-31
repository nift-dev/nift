#!/usr/bin/env python3
"""Weekend collections (D-063): map/set/stack values on the VM.

  weekend_collections.py --nift ./nift            # the suite (make test-runtime2-collections)
  weekend_collections.py --nift ./nift --update   # rewrite the approved/*.vm and *.legacy goldens

Contract: docs/runtime-2/checkpoints/weekend-collections.md.

  parity/*.f    legacy (NIFT_VM=0) and the VM (NIFT_VM=require) give the same
                stdout, stderr and exit status, and the VM ran the program
                (statistics outcome vm or vm-error, no legacy execution).
  approved/*.f  approved value-semantics differences (D-063): the VM output is
                NAME.vm, the legacy witness is NAME.legacy, and the two differ.
  refuse/*.f    refused before any effect: exit 1, no stdout (every fixture
                prints first), "no part of it ran", and the reason in NAME.reason.
  heap          budget stops before growth (map, set, stack, and the clone of a
                shared collection), exact peak accounting, and the live heap at
                stop back to an identical program's that never filled them.

Exit 0 when every check passes. A mutant binary must make it fail (mutants.sh).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "collections")


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
    # Diagnostics name the script by its absolute path: drop the per-run directory.
    norm = lambda b: b.decode("utf-8", "replace").replace(d + os.sep, "")
    return norm(p.stdout), norm(p.stderr), p.returncode, st


def transcript(out, err, rc):
    return out + "--- stderr\n" + err + "--- exit %d\n" % rc


def fixtures(kind):
    d = os.path.join(HERE, kind)
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".f"))


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

    with tempfile.TemporaryDirectory(prefix="collections-") as work:
        # ---- parity with the legacy oracle ----
        for f in fixtures("parity"):
            name = os.path.basename(f)
            lo, le, lr, _ = run(nift, f, "0", work)
            vo, ve, vr, st = run(nift, f, "require", work)
            outcome = st.get("outcome") if st else None
            check(outcome in ("vm", "vm-error") and st.get("legacy_executions", 1) == 0,
                  "parity %s: not run by the VM (outcome %s): %s" % (name, outcome, ve.strip()[:200]))
            check((lo, le, lr) == (vo, ve, vr),
                  "parity %s: legacy %r/%r/%d vs VM %r/%r/%d" % (name, lo[-300:], le[-200:], lr, vo[-300:],
                                                                  ve[-200:], vr))
        # ---- approved value-semantics differences ----
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
            outcome = st.get("outcome") if st else None
            check(outcome in ("vm", "vm-error"), "approved %s: not run by the VM (outcome %s)" % (name, outcome))
            want_vm = open(base + ".vm").read() if os.path.exists(base + ".vm") else None
            want_legacy = open(base + ".legacy").read() if os.path.exists(base + ".legacy") else None
            check(vm_t == want_vm, "approved %s: VM output\n%s\nexpected\n%s" % (name, vm_t, want_vm))
            check(legacy_t == want_legacy, "approved %s: legacy witness changed\n%s" % (name, legacy_t))
            check(vm_t != legacy_t, "approved %s: no longer differs from legacy (not an approved difference)" % name)
        # ---- refused before any effect ----
        for f in fixtures("refuse"):
            name = os.path.basename(f)
            reason = open(f[:-2] + ".reason").read().strip()
            vo, ve, vr, st = run(nift, f, "require", work)
            check(vr == 1 and vo == "", "refuse %s: exit %d stdout %r (must be 1 and empty)" % (name, vr, vo))
            check("(%s)" % reason in ve and "no part of it ran" in ve,
                  "refuse %s: expected reason %s before effects, got %r" % (name, reason, ve.strip()[:300]))
        if args.update:
            print("updated approved goldens")
            return 0
        # ---- heap accounting ----
        heap = os.path.join(HERE, "heap")
        for fname, budget in (("budget_map.f", 2_000_000), ("budget_set.f", 2_000_000),
                              ("budget_stack.f", 2_000_000), ("budget_clone.f", 2_750_000)):
            vo, ve, vr, st = run(nift, os.path.join(heap, fname), "require", work, budget=budget)
            check(vr == 70 and "memory budget exceeded" in ve and "done" not in vo,
                  "heap %s: expected the budget stop (exit 70), got exit %d %r %r" % (fname, vr, vo, ve[:200]))
            vo, ve, vr, st = run(nift, os.path.join(heap, fname), "require", work)
            check(vr == 0 and vo.endswith("done\n"), "heap %s: unbounded run failed: %d %r" % (fname, vr, ve[:200]))
        # The same map without the copy fits the budget: the stop above is the clone's.
        vo, ve, vr, st = run(nift, os.path.join(heap, "budget_noclone.f"), "require", work, budget=2_750_000)
        check(vr == 0 and vo == "done\n", "heap budget_noclone.f: expected to fit the budget: %d %r" % (vr, ve[:200]))
        vo, ve, vr, st = run(nift, os.path.join(heap, "peak_map.f"), "require", work)
        peak = st.get("heap_peak", 0) if st else 0
        # 20000 entries: two 16-byte Values each, at least 640000 bytes of item storage.
        check(vr == 0 and peak >= 20000 * 2 * 16, "heap peak_map.f: peak %d below the item storage" % peak)
        for fill, control in (("live_filled.f", "live_control.f"),):
            a = run(nift, os.path.join(heap, fill), "require", work)
            b = run(nift, os.path.join(heap, control), "require", work)
            la = a[3].get("heap_live") if a[3] else None
            lb = b[3].get("heap_live") if b[3] else None
            check(a[2] == 0 and b[2] == 0 and la is not None and la == lb,
                  "heap %s: live heap at stop %s != %s (control)" % (fill, la, lb))
    print("%s collections: %d checks, %d failure(s)" % ("PASS" if not failures else "FAIL", count, len(failures)))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
