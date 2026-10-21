#!/usr/bin/env python3
"""Focused Host-result memory contract check (CP16 N3).

Large results must be refused before the native materialises them. A small
budget plus a 60 MB sparse file (open), 20,000 directory entries (ls/glob), a
60 KB environment value (env/getenv) exercises both the pre-check and the
incremental accounting. Peak RSS is measured for the open case so a late
check is visible even if it still errors.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def run(nift, cwd, env):
    with tempfile.NamedTemporaryFile(prefix="vmhost-mem-", suffix=".log", delete=False) as log:
        log_path = log.name
    try:
        result = subprocess.run([nift, "case.f"], cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        return result, log_path
    finally:
        os.unlink(log_path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True)
    args = parser.parse_args()
    nift = os.path.abspath(args.nift)
    failures = []
    passed = 0
    with tempfile.TemporaryDirectory(prefix="vm-host-memory-") as tmp:
        root = Path(tmp)
        # open: 60 MB sparse file, 1 MB budget.
        big = root / "big.bin"
        with open(big, "wb") as handle:
            handle.truncate(60 << 20)
        (root / "case.f").write_text('print(open("big.bin").length())\n')
        env = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="1000000")
        timing = shutil.which("/usr/bin/time") or shutil.which("time")
        if timing:
            started = time.monotonic()
            proc = subprocess.run([timing, "-f", "%M", nift, "case.f"], cwd=root, env=env,
                                  stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  timeout=120)
            peak_line = proc.stderr.decode(errors="replace").strip().splitlines()
            peak = int(peak_line[-1]) if peak_line and peak_line[-1].isdigit() else None
            if proc.returncode == 0 or b"memory budget" not in proc.stderr:
                failures.append("open: expected a memory-budget refusal")
            elif peak is not None and peak > 24 * 1024:
                failures.append(f"open: peak RSS {peak} KiB > 24 MiB after late budget check")
            else:
                passed += 1
        else:
            failures.append("open: /usr/bin/time unavailable")

        # ls / glob: 20,000 names, 1 MB budget.
        names = root / "names"
        names.mkdir()
        for i in range(20000):
            (names / f"n{i:05d}").touch()
        for label, body in (("ls", 'l := ls("names"); print(l.length())\n'),
                            ("glob", 'l := ls("names/*"); print(l.length())\n')):
            (root / "case.f").write_text(body)
            proc = subprocess.run([nift, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
            if proc.returncode == 0 or b"memory budget" not in proc.stderr:
                failures.append(f"{label}: expected a memory-budget refusal")
            else:
                passed += 1

        # env / getenv: 60 KB value with a 50 KB budget.
        env_small = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="50000",
                         BIGVAR="x" * 60000)
        for label, body in (("env", "e := env(); print(type(e))\n"),
                            ("getenv", 'v := getenv("BIGVAR"); print(v != null)\n')):
            (root / "case.f").write_text(body)
            proc = subprocess.run([nift, "case.f"], cwd=root, env=env_small, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
            if proc.returncode == 0 or b"memory budget" not in proc.stderr:
                failures.append(f"{label}: expected a memory-budget refusal")
            else:
                passed += 1

    for failure in failures:
        print("FAIL", failure)
    print(f"PASS {passed}, FAIL {len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
