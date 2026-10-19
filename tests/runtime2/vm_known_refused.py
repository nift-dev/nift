#!/usr/bin/env python3
"""Known-refused programs (tests/runtime2/vm-known-refused/EXPECT): inside the
documented supported subset, still refused by the compiler. Each must be
refused before execution with its listed reason (NIFT_VM=classify), and under
NIFT_VM=1 run on legacy with legacy's exact output. A program that compiles
fails this test on purpose: widen the subset spec back (see EXPECT).

  vm_known_refused.py --nift ./nift
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vm-known-refused")


def run(nift, d, name, mode):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "HOME": d,
           "TMPDIR": d, **{k: os.environ[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS") if k in os.environ}}
    stats = os.path.join(d, ".stats")
    if os.path.exists(stats):
        os.unlink(stats)
    if mode:
        env.update({"NIFT_VM": mode, "NIFT_VM_STATS": stats})
    p = subprocess.run([nift, name], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL, timeout=120)
    st = None
    if mode and os.path.exists(stats):
        lines = open(stats).read().splitlines()
        st = json.loads(lines[-1]) if lines else None
    return (p.stdout, p.stderr, p.returncode), st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    cases = []
    for line in open(os.path.join(HERE, "EXPECT")):
        line = line.split("#")[0].strip()
        if line:
            name, reason = line.split()
            cases.append((name, reason))
    failures = []
    work = tempfile.mkdtemp(prefix="nift-vm-known-refused-")
    try:
        for name, reason in cases:
            shutil.copy(os.path.join(HERE, name), os.path.join(work, name))
            legacy, _ = run(nift, work, name, None)
            _, cst = run(nift, work, name, "classify")
            if not cst or cst["outcome"] != "fallback" or cst["reason"] != reason:
                failures.append(f"{name}: expected still refused ({reason}), got "
                                f"{cst and (cst['outcome'], cst['reason'])}: widen the subset spec back (EXPECT)")
                continue
            vm, st = run(nift, work, name, "1")
            if not st or st["outcome"] != "fallback" or st.get("vm_executions", 0) != 0 or vm != legacy:
                failures.append(f"{name}: not refused before execution or output differs from legacy")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures:
        print("FAIL " + f)
    print(("PASS" if not failures else "FAIL") + f" vm known-refused: {len(cases)} programs still refused with "
          f"their reason; {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
