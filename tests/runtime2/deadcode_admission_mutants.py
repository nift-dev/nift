#!/usr/bin/env python3
"""Negative controls of slot 3, dead-code admission (src/rt/compiler_deferred.cpp, src/rt/name_scan.cpp,
src/rt/vm_report.cpp).

For each mutant in deadcode_admission.MUTANTS, a nift built by tests/runtime2/sanctioned_mutant.py (only the
sources naming the macro differ from production) must fail tests/runtime2/deadcode_admission.py on every
case named for it:

  deadcode-1   no deferral (today's refusal, also "raised at compile time")   deadcode-2   sessions defer
  deadcode-3   name certainty skipped                                         deadcode-4   the raise does nothing
  deadcode-5   arguments evaluated before the raise                           deadcode-6   callers' return: dropped
  name-scan-1  fn/method declaration heads missed     name-scan-2  assignments missed (struct fields, P1-N)

A mutant build failure is a harness failure, never a detection. Run through make
(test-runtime2-deadcode-admission-mutants) after `make nift`; each mutant directory is deleted afterwards.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import deadcode_admission  # noqa: E402

MACROS = {"deadcode": "NIFT_DEADCODE_MUTANT", "name-scan": "NIFT_NAME_SCAN_MUTANT"}


def main():
    failures = []
    for key, expected in sorted(deadcode_admission.MUTANTS.items()):
        family, n = key.rsplit("-", 1)
        out = os.path.join(ROOT, ".build", "mutant-nift", "deadcode-" + key)
        try:
            b = subprocess.run([sys.executable, "-B", os.path.join(HERE, "sanctioned_mutant.py"), "--out", out,
                                "--define=-D%s=%s" % (MACROS[family], n)], capture_output=True, text=True)
            if b.returncode != 0:
                failures.append("%s: harness: build failed: %s" % (key, b.stderr[-400:] or b.stdout[-400:]))
                continue
            t = subprocess.run([sys.executable, "-B", os.path.join(HERE, "deadcode_admission.py"), "--nift",
                                os.path.join(out, "nift")], capture_output=True, text=True)
            failing = {l.split()[1] for l in t.stdout.splitlines() if l.startswith("FAIL ")}
            missed = [c for c in expected if c not in failing]
            if t.returncode == 0 or missed:
                failures.append("%s: not caught by %s (failing: %s)" % (key, missed or expected, sorted(failing)))
            else:
                print("PASS deadcode mutant %s: caught by %s" % (key, ", ".join(expected)))
        finally:
            shutil.rmtree(out, ignore_errors=True)
    for f in failures:
        print("FAIL deadcode mutant " + f)
    total = len(deadcode_admission.MUTANTS)
    print("%s deadcode mutants: %d of %d caught" % ("FAIL" if failures else "PASS", total - len(failures), total))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
