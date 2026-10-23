#!/usr/bin/env python3
"""Loop/closure interaction fixtures for the confirmed CP12C Slice 1 defects.

  for_s2_known_deps.py --nift ./nift

These are not loop-suite passes. On the original closure base they are expected
to fail (A1/A2/A3/A4/A5); on the corrected closure base they must become PASS.
A1 is a budget test, the rest compare the VM's bytes with the legacy oracle.
A harness failure (no statistics) is reported separately from a known
dependency failure.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "for-s2-known")

BYTES_CASES = [
    ("A2", "a2_loop_block_shadow.f"),
    ("A2", "a2_toplevel_block_shadow.f"),
    ("A3", "a3_late_local.f"),
    ("A3", "a3_lambda_in_lambda_late.f"),
    ("A4", "a4_print_unknown.f"),
    ("A4", "a4_push_unknown.f"),
    ("A5", "a5_binding_type_name.f"),
]
BUDGET_CASE = ("A1", "a1_loop_closures_budget.f")


def run(nift, work, name, mode, budget=None):
    d = tempfile.mkdtemp(dir=work, prefix=name + "-")
    shutil.copy(os.path.join(FIX, name), os.path.join(d, name))
    stats = os.path.join(d, "stats.json")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d,
           "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "NIFT_PREVALIDATE": "off"}
    if mode:
        env["NIFT_VM"] = mode
        env["NIFT_VM_STATS"] = stats
        if budget:
            env["NIFT_VM_HEAP_BUDGET"] = str(budget)
    try:
        p = subprocess.run(["timeout", "120", nift, name], cwd=d, env=env,
                           stdin=subprocess.DEVNULL, capture_output=True, timeout=135)
        result = (p.stdout.replace(d.encode(), b"$ROOT"), p.stderr.replace(d.encode(), b"$ROOT"),
                  p.returncode, False)
    except subprocess.TimeoutExpired:
        result = (b"", b"", 124, True)
    st = None
    if os.path.exists(stats):
        try:
            st = json.loads(open(stats).read().splitlines()[-1])
        except (OSError, IndexError, ValueError):
            st = None
    return result, st


def golden(name):
    stem = os.path.join(FIX, name[:-2])
    return (open(stem + ".stdout", "rb").read(), open(stem + ".stderr", "rb").read(),
            int(open(stem + ".exit").read().strip()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    known, passed, failures = [], [], 0
    with tempfile.TemporaryDirectory(prefix="for-s2-known-") as work:
        defect, name = BUDGET_CASE
        vm, st = run(nift, work, name, "require", 8000000)
        if vm[3] or st is None:
            failures += 1
            print(f"FAIL {name}: no statistics/timeout")
        elif st.get("outcome") == "vm":
            known.append((defect, name, f"under 8 MB it still succeeds (heap_peak={st.get('heap_peak')})"))
        elif st.get("outcome") == "internal-error" and "memory budget" in (st.get("reason") or ""):
            passed.append((defect, name, "memory budget refusal"))
        else:
            known.append((defect, name, f"unexpected outcome {st.get('outcome')}:{st.get('reason')}"))
        for defect, name in BYTES_CASES:
            legacy = run(nift, work, name, None)[0]
            vm, st = run(nift, work, name, "require")
            if vm[3] or st is None:
                failures += 1
                print(f"FAIL {name}: no statistics/timeout")
                continue
            if vm[:3] == golden(name):
                passed.append((defect, name, (st or {}).get("outcome") or "?"))
            else:
                known.append((defect, name, f"legacy {golden(name)!r} != VM {vm[:3]!r}"))
    for defect, name, note in passed:
        print(f"PASS {defect} {name}: {note}")
    for defect, name, note in known:
        print(f"KNOWN-DEP {defect} {name}: {note}")
    print(f"{'KNOWN-DEP' if known else 'PASS'} loop/closure interactions: "
          f"{len(passed)} pass, {len(known)} known dependency failure(s), {failures} harness failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
