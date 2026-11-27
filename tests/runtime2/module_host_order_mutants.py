#!/usr/bin/env python3
"""Negative controls of the slot-2 host-import guard (owner 2026-11-11 05:41, D-068 #2; src/rt/module_host_order.cpp).

For each N in module_host_order.MUTANTS, a nift built with -DNIFT_HOST_ORDER_MUTANT=N (by
tests/runtime2/sanctioned_mutant.py: only module_host_order.cpp differs from production) must fail
tests/runtime2/module_host_order.py on every case named for N:

  1  no leading-import-block check (E)              2  no module-initialisation check (M)
  3  calls count as safe    4  callbacks (collection operations) count as safe
  5  no shadowing check for the 11 binding-wins built-ins

A mutant build failure is a harness failure, never a detection. Run through make
(test-runtime2-module-host-order-mutants) after `make nift`; each mutant directory is deleted afterwards.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import module_host_order  # noqa: E402


def main():
    failures = []
    for n, expected in sorted(module_host_order.MUTANTS.items()):
        out = os.path.join(ROOT, ".build", "mutant-nift", "host-order-%d" % n)
        try:
            b = subprocess.run([sys.executable, "-B", os.path.join(HERE, "sanctioned_mutant.py"), "--out", out,
                                "--define=-DNIFT_HOST_ORDER_MUTANT=%d" % n], capture_output=True, text=True)
            if b.returncode != 0:
                failures.append("mutant %d: harness: build failed: %s" % (n, b.stderr[-400:] or b.stdout[-400:]))
                continue
            t = subprocess.run([sys.executable, "-B", os.path.join(HERE, "module_host_order.py"), "--nift",
                                os.path.join(out, "nift")], capture_output=True, text=True)
            failing = {l.split()[1] for l in t.stdout.splitlines() if l.startswith("FAIL ")}
            missed = [c for c in expected if c not in failing]
            if t.returncode == 0 or missed:
                failures.append("mutant %d: not caught by %s (failing: %s)" % (n, missed or expected, sorted(failing)))
            else:
                print("PASS host-order mutant %d: caught by %s" % (n, ", ".join(expected)))
        finally:
            shutil.rmtree(out, ignore_errors=True)
    for f in failures:
        print("FAIL host-order " + f)
    print("%s host-order mutants: %d of %d caught" % ("FAIL" if failures else "PASS",
                                                      len(module_host_order.MUTANTS) - len(failures),
                                                      len(module_host_order.MUTANTS)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
