#!/usr/bin/env python3
"""Weekend collections (D-063): map/set/stack on the VM. Reference semantics
(D-NEXT-refsem) superseded the value-semantics parts; their evidence is kept in
collections/superseded/ (INDEX.tsv), never run.

  weekend_collections.py --nift ./nift            # the suite (make test-runtime2-collections)
  weekend_collections.py --nift ./nift --update   # rewrite the approved/*.vm and *.legacy goldens

Contract: docs/runtime-2/checkpoints/weekend-collections.md.

  parity/*.f    legacy (NIFT_VM=0) and the VM (NIFT_VM=require) give the same
                stdout, stderr and exit status, and the VM ran the program
                (statistics outcome vm or vm-error, no legacy execution).
  approved/*.f  approved value-semantics differences (D-063): the VM output is
                NAME.vm, the legacy witness is NAME.legacy, and the two differ.
                NAME.legacy-undefined instead of NAME.legacy: legacy's behaviour is
                undefined there (it reads a removed map entry), so it is not run.
  refuse/*.f    refused before any effect: exit 1, no stdout (every fixture
                prints first), "no part of it ran", the reason in NAME.reason,
                and (NAME.at, "line:column") the reported position; NAME.with
                lists imported files copied next to the program.
  sessions      piped sessions and `nift eval` (SESSIONS below): a refused
                statement runs no part of itself (earlier statements' output
                stays, nothing later runs, exit 1, reason and position), and
                admitted collection units match legacy.
  heap          budget stops before growth (map, set, stack; a shared map is
                mutated in place, D-NEXT-refsem), exact peak accounting, and the
                live heap at stop back to an identical program's that never
                filled them.

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
    # NAME.with: further files (relative to the fixture's directory) the
    # program imports, copied next to it under their base names.
    with_list = path[:-2] + ".with"
    if os.path.exists(with_list):
        for extra_file in open(with_list).read().split():
            shutil.copy(os.path.join(os.path.dirname(path), extra_file), os.path.join(d, os.path.basename(extra_file)))
    stats = os.path.join(d, "stats.jsonl")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C",
           "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": mode, "NIFT_VM_STATS": stats}
    if budget is not None:
        env["NIFT_VM_HEAP_BUDGET"] = str(budget)
    if extra:
        env.update(extra)
    p = subprocess.run([os.environ.get("NIFT_TEST_TIMEOUT", "timeout"), "120", nift, name], cwd=d, env=env, stdin=subprocess.DEVNULL,
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


def run_session(nift, stdin, work, argv=None, mode="require"):
    d = tempfile.mkdtemp(dir=work)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C",
           "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": mode}
    p = subprocess.run([os.environ.get("NIFT_TEST_TIMEOUT", "timeout"), "120", nift] + (argv or []), cwd=d, env=env, input=stdin.encode(),
                       capture_output=True)
    return p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"), p.returncode


# (name, stdin, argv, expectation): ("refuse", stdout before the refusal,
# reason, "line:column"), ("parity",) or ("vm", stdout) (B4). argv None: a piped session.
SESSIONS = [
    # B4 (b4-dispatch.md 6): a session shows a map/set/stack value itself in its
    # compact form (legacy parity); legacy's tag inside a line is not reproduced.
    ("collection_statement", 'print("a")\nmap()\nprint("b")\n', None, ("parity",)),
    ("collection_in_line", 'print("a")\nprint(1); set()\nprint("b")\n', None, ("vm", "a\n1\nnullset([])\nb\n")),
    ("collection_nested", 'print("a")\n[map()]\nprint("b")\n', None,
     ("refuse", "a\n", "session:collection-result", "<stdin>:2:1")),
    ("collection_call_result", 'print("a")\nf := () => stack()\nf()\nprint("b")\n', None, ("parity",)),
    ("mutator_echo", 'print("a")\nm := map(); m.set(1, 2); print(m.size())\nprint("b")\n', None,
     ("refuse", "a\n", "session:collection-mutator", "<stdin>:2:13")),
    ("same_string_literal", 'print("a")\nf := () => 1; print(same("f", "f"))\nprint("b")\n', None,
     ("refuse", "a\n", "same:argument-shape", "<stdin>:2:26")),
    ("eval_collection", "", ["eval", "map()"], ("refuse", "", "eval:collection-result", "<eval>:1:1")),
    ("eval_nested_collection", "", ["eval", "[set()]"], ("refuse", "", "eval:collection-result", "<eval>:1:1")),
    # Plain uses still run.
    ("scalar_of_collection", 'print("a")\nm := map(); m.size()\nprint("b")\n', None, ("parity",)),
    ("print_in_session", 'print("a")\nm := map(); print(m.size()); print(m.stringify())\nprint("b")\n', None,
     ("parity",)),
    ("eval_scalar_of_collection", "", ["eval", "type(map())"], ("parity",)),
]


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
            undefined = os.path.exists(base + ".legacy-undefined")
            lo, le, lr = run(nift, f, "0", work)[:3] if not undefined else ("", "", 0)
            vm_t, legacy_t = transcript(vo, ve, vr), transcript(lo, le, lr)
            if args.update:
                open(base + ".vm", "w").write(vm_t)
                if not undefined:
                    open(base + ".legacy", "w").write(legacy_t)
                continue
            outcome = st.get("outcome") if st else None
            check(outcome in ("vm", "vm-error"), "approved %s: not run by the VM (outcome %s)" % (name, outcome))
            want_vm = open(base + ".vm").read() if os.path.exists(base + ".vm") else None
            want_legacy = open(base + ".legacy").read() if os.path.exists(base + ".legacy") else None
            check(vm_t == want_vm, "approved %s: VM output\n%s\nexpected\n%s" % (name, vm_t, want_vm))
            if undefined:
                continue
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
            if os.path.exists(f[:-2] + ".at"):
                at = "%s:%s: not compiled" % (name, open(f[:-2] + ".at").read().strip())
                check(ve.startswith("error: " + at), "refuse %s: expected position %s, got %r" % (name, at, ve[:200]))
        # ---- sessions and eval ----
        for name, stdin, argv, want in SESSIONS:
            vo, ve, vr = run_session(nift, stdin, work, argv)
            if want[0] == "vm":  # B4: the VM's transcript where legacy shows an internal tag
                check(vo == want[1] and "refused" not in ve, "session %s: VM %r/%r/%d" % (name, vo, ve, vr))
                continue
            if want[0] == "parity":
                lo, le, lr = run_session(nift, stdin, work, argv, mode="0")
                check((vo, ve, vr) == (lo, le, lr) and "not compiled" not in ve and "refused" not in ve,
                      "session %s: legacy %r/%r/%d vs VM %r/%r/%d" % (name, lo, le, lr, vo, ve, vr))
                continue
            _, before, reason, at = want
            check(vr == 1 and vo == before, "session %s: exit %d stdout %r (want 1, %r)" % (name, vr, vo, before))
            check(ve.startswith("error: %s: " % at) and reason in ve,
                  "session %s: expected %s at %s, got %r" % (name, reason, at, ve[:300]))
        if args.update:
            print("updated approved goldens")
            return 0
        # ---- heap accounting ----
        heap = os.path.join(HERE, "heap")
        for fname, budget in (("budget_map.f", 2_000_000), ("budget_set.f", 2_000_000),
                              ("budget_stack.f", 2_000_000)):
            vo, ve, vr, st = run(nift, os.path.join(heap, fname), "require", work, budget=budget)
            check(vr == 70 and "memory budget exceeded" in ve and "done" not in vo,
                  "heap %s: expected the budget stop (exit 70), got exit %d %r %r" % (fname, vr, vo, ve[:200]))
            vo, ve, vr, st = run(nift, os.path.join(heap, fname), "require", work)
            check(vr == 0 and vo.endswith("done\n"), "heap %s: unbounded run failed: %d %r" % (fname, vr, ve[:200]))
        # D-NEXT-refsem #1 (superseding the D-063 clone stop): a shared map is
        # mutated in place, so `b := a; b.set(...)` fits the budget of the
        # same map without the copy (budget_noclone.f): no clone is ever made.
        for fname in ("budget_clone.f", "budget_noclone.f"):
            vo, ve, vr, st = run(nift, os.path.join(heap, fname), "require", work, budget=2_750_000)
            check(vr == 0 and vo == "done\n", "heap %s: expected to fit the budget: %d %r" % (fname, vr, ve[:200]))
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
