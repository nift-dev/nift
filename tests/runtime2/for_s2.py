#!/usr/bin/env python3
"""CP12C Slice 2 default `for` differential fixtures and mutants.

  for_s2.py --nift ./nift
  for_s2.py --nift ./nift --normal ./nift --mutant N

Fixtures live in `for-s2/`: EXPECT pins the outcome (`vm`, `vm-error` or a
named pre-existing `fallback:`), and `.stdout/.stderr/.exit` are the legacy
oracle. Executable fixtures must run on the VM (`NIFT_VM=require`,
`vm_executions = 1`). Mutants are compiling `-DNIFT_FOR_MUTANT=N` builds; a
missing binary or missing stats is a harness failure, never a detection.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

import mutant_provenance

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "for-s2")

# The intended observable defect of each mutant, not just "something failed".
MUTANT_SIGNATURES = {
    1: {"t26_read_after_shrink_first_iter.f": {"stdout": b"1\n2\n3\n", "exit": 0, "outcome": "vm"}},
    2: {"g6_push_once.f": {"stdout": b"1\n2\n3\n4\n", "exit": 0, "outcome": "vm"}},
    3: {"g7_closure_loop_var.f": {"stdout": b"3\n3\n3\n", "exit": 0, "outcome": "vm"}},
    4: {"g3_continue_finite.f": {"stdout": b"1\n2\n", "exit": 0, "outcome": "vm"}},
    5: {"g6_break_object.f": {"stdout": b"a\n", "exit": 0, "outcome": "vm", "heap_leak_gt": 32}},
    6: {"g2_object_order.f": {"stdout": b"2\nb\n1\na\n3\nc\n", "exit": 0, "outcome": "vm"},
        "g1_object.f": {"stdout": b"2\nb\n1\na\n", "exit": 0, "outcome": "vm"}},
    7: {"g2_object_order.f": {"stdout": b"c\n3\na\n1\nb\n2\n", "exit": 0, "outcome": "vm"}},
}

MUTANT_CASES = {
    1: ["t26_read_after_shrink_first_iter.f"],
    2: ["g6_push_once.f"],
    3: ["g7_closure_loop_var.f"],
    4: ["g3_continue_finite.f"],
    5: ["g6_break_object.f"],
    6: ["g2_object_order.f", "g1_object.f"],
    7: ["g2_object_order.f"],
}


def cases():
    out = []
    for line in open(os.path.join(FIXTURES, "EXPECT")):
        line = line.split("#")[0].strip()
        if not line:
            continue
        parts = line.split()
        name, want = parts[0], parts[1]
        budget = None
        for p in parts[2:]:
            if p.startswith("budget="):
                budget = p.split("=", 1)[1]
        out.append((name + ".f", want, budget))
    return out


def run(nift, work, name, want, budget=None, timeout_s=60):
    d = tempfile.mkdtemp(dir=work, prefix=name + "-")
    shutil.copy(os.path.join(FIXTURES, name), os.path.join(d, name))
    module = os.path.join(FIXTURES, name[:-2] + ".m.nft")
    if os.path.exists(module):
        shutil.copy(module, os.path.join(d, "m.nft"))
    stats_path = os.path.join(d, "stats.json")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d,
           "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "NIFT_PREVALIDATE": "off"}
    mode = "1" if want.startswith("fallback:") else "require"
    env.update({"NIFT_VM": mode, "NIFT_VM_STATS": stats_path})
    if budget:
        env["NIFT_VM_HEAP_BUDGET"] = budget
    try:
        p = subprocess.run(["timeout", str(timeout_s), nift, name], cwd=d, env=env,
                           stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout_s + 15)
        result = (p.stdout.replace(d.encode(), b"$ROOT"), p.stderr.replace(d.encode(), b"$ROOT"),
                  p.returncode, p.returncode == 124)
    except subprocess.TimeoutExpired:
        result = (b"", b"", 124, True)
    st = None
    if os.path.exists(stats_path):
        try:
            st = json.loads(open(stats_path).read().splitlines()[-1])
        except (OSError, IndexError, ValueError):
            st = None
    return result, st


def golden(name):
    stem = os.path.join(FIXTURES, name[:-2])
    return (open(stem + ".stdout", "rb").read(), open(stem + ".stderr", "rb").read(),
            int(open(stem + ".exit").read().strip()))


def check(nift, work, name, want, budget=None, timeout_s=60):
    vm, st = run(nift, work, name, want, budget, timeout_s)
    g = golden(name)
    problems = []
    if vm[3]:
        return [f"timeout after {timeout_s}s"], st
    if st is None:
        return ["no NIFT_VM=1 statistics"], st
    if (vm[0], vm[1], vm[2]) != g:
        problems.append(f"legacy {g!r} != VM {(vm[0], vm[1], vm[2])!r}")
    outcome = st.get("outcome")
    if want == "vm":
        if outcome != "vm" or st.get("vm_executions") != 1:
            problems.append(f"expected vm, got {outcome}/{st.get('reason')} vm_executions={st.get('vm_executions')}")
    elif want == "vm-error":
        if outcome != "vm-error" or st.get("vm_executions") != 1:
            problems.append(f"expected vm-error, got {outcome}/{st.get('reason')} vm_executions={st.get('vm_executions')}")
    elif want.startswith("fallback:"):
        reason = want.split(":", 1)[1]
        if outcome != "fallback" or st.get("reason") != reason or st.get("vm_executions") != 0:
            problems.append(f"expected fallback:{reason}, got {outcome}/{st.get('reason')} "
                            f"vm_executions={st.get('vm_executions')}")
    else:
        problems.append(f"bad EXPECT mode {want!r}")
    return problems, st


def check_mutant(nift, normal, work, name, want, budget, timeout_s, mutant):
    """Run one designated mutant case. Returns (detected, harness_failure, note)."""
    if normal is None:
        return False, True, "--normal is required for mutant detection"
    vm, st = run(nift, work, name, want, budget, timeout_s)
    if vm[3]:
        return False, True, f"timeout after {timeout_s}s"
    if st is None:
        return False, True, "no statistics"
    if vm[2] < 0 or vm[2] >= 128 or vm[2] in (124, 125):
        return False, True, f"abnormal exit {vm[2]} (not a semantic mismatch)"
    if st.get("outcome") not in ("vm", "vm-error", "internal-error") or st.get("vm_executions") != 1:
        return False, True, f"invalid statistics (outcome={st.get('outcome')}, vm_executions={st.get('vm_executions')})"
    normal_vm, normal_st = run(normal, work, name, want, budget, timeout_s)
    if normal_vm[3] or normal_st is None or normal_st.get("outcome") not in ("vm", "vm-error"):
        return False, True, "unmutated baseline did not execute"
    baseline_problems, _ = check(normal, work, name, want, budget, timeout_s)
    if baseline_problems:
        return False, True, "unmutated baseline fails: " + "; ".join(baseline_problems)
    sig = MUTANT_SIGNATURES[mutant][name]
    if vm[0] != sig["stdout"]:
        return False, False, f"mutation signature stdout {vm[0]!r} != {sig['stdout']!r}"
    if vm[2] != sig["exit"] or st.get("outcome") != sig["outcome"]:
        return False, False, f"mutation signature exit/outcome {vm[2]}/{st.get('outcome')} != {sig['exit']}/{sig['outcome']}"
    if "heap_leak_gt" in sig:
        leak = st.get("heap_live", 0) - normal_st.get("heap_live", 0)
        if leak <= sig["heap_leak_gt"]:
            return False, False, f"no heap_live leak (mutant {st.get('heap_live')}, normal {normal_st.get('heap_live')})"
        return True, False, f"heap_live leak {leak} bytes after break"
    if st.get("reason") != "":
        return False, False, f"unexpected mutation reason {st.get('reason')!r}"
    return True, False, "expected semantic defect"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--normal")
    ap.add_argument("--mutant", type=int, choices=sorted(MUTANT_CASES))
    ap.add_argument("--mutant-info")
    ap.add_argument("--skip-million", action="store_true")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    normal = os.path.abspath(args.normal) if args.normal else None
    if args.mutant:
        _, provenance_error = mutant_provenance.verify(nift, args.mutant, args.mutant_info)
        if provenance_error:
            print(f"FAIL for-s2 mutant {args.mutant}: harness: {provenance_error}")
            return 1

    if args.mutant:
        failures, detected, notes = 0, [], []
        with tempfile.TemporaryDirectory(prefix="for-s2-mutant-") as work:
            for name in MUTANT_CASES[args.mutant]:
                want, budget = None, None
                for n, w, b in cases():
                    if n == name:
                        want, budget = w, b
                        break
                if want is None:
                    failures += 1
                    notes.append(f"{name}: not in EXPECT")
                    continue
                timeout_s = 30
                ok, harness_failure, note = check_mutant(nift, normal, work, name, want, budget, timeout_s,
                                                          args.mutant)
                if ok:
                    detected.append(name)
                if harness_failure:
                    failures += 1
                notes.append(f"{name}: {note}")
        missed = sorted(set(MUTANT_CASES[args.mutant]) - set(detected))
        for note in notes:
            print(f"  {note}")
        if failures or missed:
            print(f"FAIL for-s2 mutant {args.mutant}: missed {missed}; {failures} harness failure(s)")
            return 1
        print(f"PASS for-s2 mutant {args.mutant}: detected in {len(detected)} case(s)")
        return 0

    failures, tally = 0, {}
    with tempfile.TemporaryDirectory(prefix="for-s2-") as work:
        selected = [c for c in cases() if not (args.skip_million and c[0].startswith("g9_million"))]
        for name, want, budget in selected:
            problems, st = check(nift, work, name, want, budget)
            outcome = (st or {}).get("outcome") or "?"
            tally[outcome] = tally.get(outcome, 0) + 1
            if problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name} ({want}, {outcome})")
    print(f"{'PASS' if not failures else 'FAIL'} for-s2 differential: {len(selected)} cases, "
          f"{failures} failure(s); outcomes {dict(sorted(tally.items(), key=str))}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
