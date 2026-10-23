#!/usr/bin/env python3
"""Negative control for the closure mutation harness (review R2).

A mutant that crashes before writing NIFT_VM statistics must be a harness
failure, never a detection.  The wrapper runs legacy normally and crashes only
for the VM execution of every case.
"""
import argparse
import os
import subprocess
import sys
import tempfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    with tempfile.TemporaryDirectory(prefix="closure-harness-") as d:
        fake = os.path.join(d, "crashing-nift")
        with open(fake, "w") as f:
            f.write("#!/bin/sh\n")
            f.write('case "${NIFT_VM:-}" in 1|require|classify|classify-all) exit 139;; esac\n')
            f.write('exec "$NIFT_REAL" "$@"\n')
        os.chmod(fake, 0o755)
        def run(script):
            env = dict(os.environ, NIFT_REAL=nift)
            p = subprocess.run(
                [sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "closures_s1.py"),
                 "--nift", script, "--mutant", "2"],
                env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
            return p, (p.stdout or "") + (p.stderr or "")

        p, out = run(fake)
        if p.returncode == 0 or "harness" not in out:
            print("FAIL mutant harness: a crash/no-statistics run was accepted or not reported as harness")
            print(out[-2000:])
            return 1
        with open(fake, "w") as f:
            f.write("#!/bin/sh\n")
            f.write('case "${NIFT_VM:-}" in 1|require|classify|classify-all)\n')
            f.write("  [ -n \"${NIFT_VM_STATS:-}\" ] && printf '%s\\n' '{\"outcome\":\"vm\",\"reason\":\"\",\"vm_executions\":1,\"heap_peak\":1,\"heap_live\":0}' > \"$NIFT_VM_STATS\"\n")
            f.write('  exit 139;;\n')
            f.write('esac\nexec "$NIFT_REAL" "$@"\n')
        os.chmod(fake, 0o755)
        p, out = run(fake)
        if p.returncode == 0 or "harness" not in out:
            print("FAIL mutant harness: a post-statistics crash was credited as a detection")
            print(out[-2000:])
            return 1
        print("PASS mutant harness: crash/no-statistics is a harness failure, not a detection")
        return 0


if __name__ == "__main__":
    sys.exit(main())
