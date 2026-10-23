#!/usr/bin/env python3
"""Permanent consumer controls for cp16_n1_result.py.

A PASS phrase or zero exit is not evidence. Only one complete, exact record may
pass; malformed, empty, forged, duplicated, missing and wrong-transition
records must be rejected.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
VALIDATOR = HERE / "cp16_n1_result.py"
VALID = {
    "schema": "cp16-n1-mutant-result/v1",
    "checks": 16,
    "read_code_mismatches": [
        {"native": "open", "actual": "io.open_failed", "expected": "io.read_failed"},
        {"native": "cat", "actual": "io.open_failed", "expected": "io.read_failed"},
        {"native": "sha256_file", "actual": "io.open_failed", "expected": "io.read_failed"},
    ],
    "open_failure_codes": [],
    "harness_failures": [],
    "legacy_failures": [],
    "other_failures": [],
    "skipped_read_cases": 0,
}


def main():
    cases = {
        "valid": "MUTANT-RESULT " + json.dumps(VALID) + "\n",
        "malformed": "MUTANT-RESULT not-json\n",
        "empty-object": "MUTANT-RESULT {}\n",
        "forged": "MUTANT-RESULT " + json.dumps(dict(VALID, checks=0,
                                                       harness_failures=["boom"])) + "\n",
        "duplicate": "MUTANT-RESULT " + json.dumps(VALID) + "\nMUTANT-RESULT " + json.dumps(VALID) + "\n",
        "missing": "PASS N1 mutant predicate: fake\n",
        "wrong-transition": "MUTANT-RESULT " + json.dumps(dict(
            VALID, read_code_mismatches=[{"native": n, "actual": "totally.wrong",
                                          "expected": "io.read_failed"}
                                         for n in ("open", "cat", "sha256_file")])) + "\n",
        "wrong-types": "MUTANT-RESULT " + json.dumps(dict(VALID, checks="16")) + "\n",
        "skipped-bool": "MUTANT-RESULT " + json.dumps(dict(VALID, skipped_read_cases=False)) + "\n",
        "skipped-float": "MUTANT-RESULT " + json.dumps(dict(VALID, skipped_read_cases=0.0)) + "\n",
        "pass-phrase-with-invalid": "PASS N1 mutant predicate: exact\nMUTANT-RESULT {}\n",
    }
    failures = 0
    with tempfile.TemporaryDirectory(prefix="cp16-n1-consumer-") as folder:
        folder = Path(folder)
        for name, text in cases.items():
            log = folder / f"{name}.log"
            log.write_text(text)
            result = subprocess.run([sys.executable, str(VALIDATOR), "--log", str(log),
                                     "--require-mutant"], capture_output=True, text=True)
            should_pass = name == "valid"
            passed = result.returncode == 0
            if passed != should_pass:
                print(f"FAIL consumer control {name}: rc={result.returncode}\n{result.stdout[-1000:]}")
                failures += 1
            else:
                print(f"ok   consumer control {name}: {'accepted' if should_pass else 'rejected'}")
    with tempfile.TemporaryDirectory(prefix="cp16-n1-boundary-") as folder:
        folder = Path(folder)
        valid_log = folder / "valid.log"
        invalid_log = folder / "invalid.log"
        valid_log.write_text("MUTANT-RESULT " + json.dumps(VALID) + "\n")
        invalid_log.write_text("MUTANT-RESULT {}\n")

        def boundary(status, log):
            # Same gate as host_mutants M14: producer status must be zero, then
            # the validator must accept the record.
            script = f'[ "{status}" -eq 0 ] || exit 1; exec "{sys.executable}" "{VALIDATOR}" --log "{log}" --require-mutant'
            return subprocess.run(["bash", "-c", script], capture_output=True, text=True)

        boundary_cases = [
            ("status-0-valid", 0, valid_log, True),
            ("status-1-valid-record", 1, valid_log, False),
            ("status-137-valid-record", 137, valid_log, False),
            ("status-0-invalid-record", 0, invalid_log, False),
        ]
        for name, status, log, should_pass in boundary_cases:
            result = boundary(status, log)
            passed = result.returncode == 0
            if passed != should_pass:
                print(f"FAIL boundary control {name}: rc={result.returncode}\n{result.stdout[-1000:]}")
                failures += 1
            else:
                print(f"ok   boundary control {name}: {'accepted' if should_pass else 'rejected'}")
        checks = len(cases) + len(boundary_cases)
    print(f"{'PASS' if failures == 0 else 'FAIL'} N1 consumer controls: {checks} checks, "
          f"{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
