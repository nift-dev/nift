#!/usr/bin/env python3
"""R4: the native assertion negative control must be the deliberate failed
CHECK, not any unrelated nonzero exit/crash/missing executable."""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def accepted(rc, output):
    return (rc != 0 and "CHECK failed: false" in output and
            "1 checks, 1 failures" in output)


def run(cmd, env=None):
    try:
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=30)
        return proc.returncode, proc.stdout + proc.stderr
    except (OSError, subprocess.TimeoutExpired) as error:
        return 127, str(error)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--unit", required=True)
    args = ap.parse_args()
    unit = os.path.abspath(args.unit)
    env = dict(os.environ, NIFT_HOST_NATIVE_NEGATIVE="1")
    rc, output = run([unit], env)
    failures = 0
    if not accepted(rc, output):
        print(f"FAIL native assertion witness: actual control rc={rc} output={output!r}")
        failures += 1
    with tempfile.TemporaryDirectory(prefix="cp17-assertion-") as tmp:
        plain = Path(tmp) / "plain.sh"
        plain.write_text("#!/bin/sh\nexit 1\n")
        plain.chmod(0o755)
        crash = Path(tmp) / "crash.sh"
        crash.write_text("#!/bin/sh\nkill -SEGV $$\n")
        crash.chmod(0o755)
        cases = [
            ("unrelated exit", [str(plain)]),
            ("crash", [str(crash)]),
            ("missing executable", [str(Path(tmp) / "missing")]),
        ]
        for label, cmd in cases:
            case_rc, case_out = run(cmd)
            if accepted(case_rc, case_out):
                print(f"FAIL native assertion witness: {label} was accepted")
                failures += 1
            else:
                print(f"ok   {label}: rejected")
    print("PASS native assertion witness" if failures == 0 else
          f"FAIL native assertion witness: {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
