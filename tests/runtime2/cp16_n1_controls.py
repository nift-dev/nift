#!/usr/bin/env python3
"""Permanent executable controls for the CP16 N1 harness (B2/B3).

Each fake binary injects one false-pass class. The N1 test must reject every
normal-mode fake; the structured --expect-mutant predicate must accept only the
exact simulated per-built-in mapping (Linux read cases) and reject a mismatch
followed by a harness error, missing/malformed stats, signals, timeouts and a
non-working binary.
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
N1 = HERE / "cp16_n1.py"
LINUX_READ = sys.platform == "linux" and os.path.exists("/proc/self/mem")

FAKE = r'''#!/usr/bin/env python3
import json, os, signal, subprocess, sys, time
MODE = "__MODE__"
REAL = "__REAL__"
source = ""
try:
    with open(sys.argv[1]) as fh:
        source = fh.read()
except OSError:
    pass
vm = os.environ.get("NIFT_VM") == "require"
missing_open_bytes = vm and "open_bytes(" in source and "missing-n1-file" in source
if MODE == "stale" and missing_open_bytes:
    sys.exit(137)
if MODE == "nostats" and missing_open_bytes:
    env = dict(os.environ); env.pop("NIFT_VM_STATS", None)
    sys.exit(subprocess.run([REAL] + sys.argv[1:], env=env).returncode)
if MODE == "malformed" and missing_open_bytes:
    stats = os.environ.get("NIFT_VM_STATS")
    if stats:
        with open(stats, "a") as fh: fh.write("{not json\n")
    sys.exit(1)
if MODE == "crash" and missing_open_bytes:
    os.kill(os.getpid(), signal.SIGSEGV)
if MODE == "timeout" and missing_open_bytes:
    time.sleep(30)
if MODE == "mismatch_then_harness" and not vm and "x := open(" in source and "missing-n1-file" in source:
    print("malformed-oracle-output")
    sys.exit(0)
if vm and MODE in ("wrong_code", "bad_diag") and "/proc/self/mem" in source:
    proc = subprocess.run([REAL] + sys.argv[1:], capture_output=True)
    stats = os.environ.get("NIFT_VM_STATS")
    if stats and os.path.exists(stats):
        rows = [line for line in open(stats).read().splitlines() if line.strip()]
        if rows:
            data = json.loads(rows[-1])
            data["code"] = "totally.wrong" if MODE == "wrong_code" else "io.open_failed"
            if MODE == "bad_diag":
                data["reason"] = "WRONG DIAGNOSTIC"
            with open(stats, "w") as fh: fh.write(json.dumps(data) + "\n")
    sys.stdout.buffer.write(proc.stdout)
    if MODE == "bad_diag":
        sys.stderr.write("WRONG DIAGNOSTIC\n")
    else:
        sys.stderr.buffer.write(proc.stderr)
    sys.exit(proc.returncode)
proc = subprocess.run([REAL] + sys.argv[1:])
if vm and MODE in ("mutant", "mismatch_then_harness") and "/proc/self/mem" in source:
    stats = os.environ.get("NIFT_VM_STATS")
    if stats and os.path.exists(stats):
        rows = [line for line in open(stats).read().splitlines() if line.strip()]
        if rows:
            data = json.loads(rows[-1])
            data["code"] = "io.open_failed"
            with open(stats, "w") as fh: fh.write(json.dumps(data) + "\n")
sys.exit(proc.returncode)
'''


def make(folder, mode, real):
    path = folder / f"nift-{mode}"
    path.write_text(FAKE.replace("__MODE__", mode).replace("__REAL__", real))
    path.chmod(0o755)
    return str(path)


def invoke(nift, *extra, timeout=90):
    return subprocess.run([sys.executable, str(N1), "--nift", nift] + list(extra),
                          capture_output=True, text=True, timeout=timeout)


def main():
    real = str(Path(sys.argv[sys.argv.index("--nift") + 1]).resolve())
    failures = 0
    checks = 0
    with tempfile.TemporaryDirectory(prefix="cp16-n1-controls-") as folder:
        folder = Path(folder)
        fakes = {mode: make(folder, mode, real)
                 for mode in ("stale", "nostats", "malformed", "crash", "timeout",
                              "mutant", "wrong_code", "bad_diag", "mismatch_then_harness")}

        expected_reason = {
            "stale": "missing NIFT_VM_STATS",
            "nostats": "missing NIFT_VM_STATS",
            "malformed": "malformed NIFT_VM_STATS",
            "crash": "missing NIFT_VM_STATS",
            "timeout": "timeout",
        }

        def normal_rejected(name, nift, extra_timeout=False):
            nonlocal failures, checks
            checks += 1
            extra = ["--timeout", "2"] if extra_timeout else []
            result = invoke(nift, *extra)
            reason = expected_reason.get(name)
            if result.returncode == 0 or "PASS N1 per-call codes" in result.stdout:
                print(f"FAIL control {name}: normal N1 test passed\n{result.stdout[-1500:]}")
                failures += 1
            elif reason and reason not in result.stdout:
                print(f"FAIL control {name}: rejected without the intended {reason!r}\n{result.stdout[-1500:]}")
                failures += 1
            else:
                print(f"ok   control {name}: rejected for the intended reason")

        def mutant_rejected(name, nift, extra_timeout=False, needs=""):
            nonlocal failures, checks
            checks += 1
            extra = ["--expect-mutant", "--timeout", "2"] if extra_timeout else ["--expect-mutant"]
            result = invoke(nift, *extra)
            line = next((l for l in result.stdout.splitlines() if l.startswith("MUTANT-RESULT ")), "")
            try:
                data = json.loads(line[len("MUTANT-RESULT "):]) if line else {}
            except ValueError:
                data = {}
            bad = result.returncode == 0 or "PASS N1 mutant predicate" in result.stdout or not data
            if not bad and needs == "harness":
                bad = not data.get("harness_failures")
            if not bad and needs == "legacy_or_harness":
                bad = not (data.get("harness_failures") or data.get("legacy_failures"))
            if not bad and needs == "other":
                bad = not data.get("other_failures")
            if not bad and needs == "mismatch":
                bad = not data.get("read_code_mismatches")
            if bad:
                print(f"FAIL control {name}: mutant predicate not rejected for {needs or 'no mismatch'}\n"
                      f"{result.stdout[-1500:]}")
                failures += 1
            else:
                print(f"ok   control {name}: mutant predicate rejected structurally (rc={result.returncode})")

        # The unmutated binary is the baseline.
        checks += 1
        base = invoke(real)
        if base.returncode != 0 or "PASS N1 per-call codes" not in base.stdout:
            print(f"FAIL control baseline: normal N1 test failed\n{base.stdout[-1500:]}")
            failures += 1
        else:
            print("ok   control baseline: unmutated N1 test PASS")

        for mode in ("stale", "nostats", "malformed", "crash", "timeout",
                     "wrong_code", "bad_diag"):
            normal_rejected(mode, fakes[mode], extra_timeout=(mode == "timeout"))
            needs = "harness" if mode in ("stale", "nostats", "malformed", "crash", "timeout") else (
                "mismatch" if mode == "wrong_code" else "other")
            mutant_rejected(mode, fakes[mode], extra_timeout=(mode == "timeout"), needs=needs)
        normal_rejected("false", "/bin/false")
        mutant_rejected("false", "/bin/false", needs="legacy_or_harness")
        mutant_rejected("real-no-mismatch", real)
        mutant_rejected("mismatch-then-harness", fakes["mismatch_then_harness"],
                        needs="legacy_or_harness")
        if LINUX_READ:
            normal_rejected("simulated-mutant", fakes["mutant"])
            checks += 1
            expected = {
                ("open", "io.open_failed", "io.read_failed"),
                ("cat", "io.open_failed", "io.read_failed"),
                ("sha256_file", "io.open_failed", "io.read_failed"),
            }
            result = invoke(fakes["mutant"], "--expect-mutant")
            line = next((l for l in result.stdout.splitlines() if l.startswith("MUTANT-RESULT ")), "")
            try:
                data = json.loads(line[len("MUTANT-RESULT "):]) if line else {}
            except ValueError:
                data = {}
            got = {(m.get("native"), m.get("actual"), m.get("expected"))
                   for m in data.get("read_code_mismatches", []) if isinstance(m, dict)}
            if result.returncode != 0 or data.get("schema") != "cp16-n1-mutant-result/v1" or \
                    data.get("checks") != 16 or data.get("skipped_read_cases") != 0 or \
                    got != expected or data.get("harness_failures") or data.get("legacy_failures") or \
                    data.get("open_failure_codes") or data.get("other_failures"):
                print(f"FAIL control simulated-mutant: wrong structured result\n{result.stdout[-1500:]}")
                failures += 1
            else:
                print("ok   control simulated-mutant: exact three read-code mismatches accepted")
        else:
            print("SKIP control simulated-mutant: Linux read-failure fixture unavailable")
    print(f"{'PASS' if failures == 0 else 'FAIL'} N1 harness controls: {checks} checks, "
          f"{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
