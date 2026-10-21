#!/usr/bin/env python3
"""Focused Host-result memory contract check (CP16 N3a/N3b).

Small budgets force open(), unknown-size reads (/dev/stdin and a FIFO), ls,
glob and env/getenv to stop before materialising their full result. Peak RSS is
checked for each case so a late check is visible even when it errors.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


def timed(nift, cwd, env, input_bytes=None):
    timing = shutil.which("/usr/bin/time")
    if not timing:
        return None, b"", b"/usr/bin/time unavailable"
    proc = subprocess.run([timing, "-f", "%M", nift, "case.f"], cwd=cwd, env=env,
                          input=input_bytes, stdin=subprocess.DEVNULL if input_bytes is None else None,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    lines = proc.stderr.decode(errors="replace").strip().splitlines()
    peak = int(lines[-1]) if lines and lines[-1].isdigit() else None
    return peak, proc.stdout, proc.stderr


def check(label, peak, proc, failures):
    if proc is None:
        failures.append(f"{label}: /usr/bin/time unavailable")
        return False
    if proc.returncode == 0 or b"memory budget" not in proc.stderr:
        failures.append(f"{label}: expected a memory-budget refusal")
        return False
    if peak is None or peak <= 0:
        failures.append(f"{label}: peak RSS was not measured")
        return False
    if peak > 24 * 1024:
        failures.append(f"{label}: peak RSS {peak} KiB > 24 MiB")
        return False
    print(f"PEAK {label} {peak} KiB")
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True)
    args = parser.parse_args()
    nift = os.path.abspath(args.nift)
    failures = []
    passed = 0
    budget = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="1000000")
    with tempfile.TemporaryDirectory(prefix="vm-host-memory-") as tmp:
        root = Path(tmp)
        timing = shutil.which("/usr/bin/time")
        if not timing:
            print("SKIP vm_host_memory: /usr/bin/time unavailable")
            return 0

        # open: 60 MB regular file, 1 MB budget.
        with open(root / "big.bin", "wb") as handle:
            handle.truncate(60 << 20)
        (root / "case.f").write_text('print(open("big.bin").length())\n')
        peak, _, err = timed(nift, root, budget)
        if check("open", peak, subprocess.CompletedProcess([], 1 if b"memory budget" in err else 0, b"", err),
                 failures):
            passed += 1

        # Unknown-size read: 50 MB on /dev/stdin.
        (root / "case.f").write_text('print(open("/dev/stdin").length())\n')
        peak, _, err = timed(nift, root, budget, input_bytes=b"x" * (50 << 20))
        if check("stdin", peak, subprocess.CompletedProcess([], 1 if b"memory budget" in err else 0, b"", err),
                 failures):
            passed += 1

        # Unknown-size read: 50 MB through a FIFO.
        fifo = root / "feed.fifo"
        if hasattr(os, "mkfifo"):
            os.mkfifo(fifo)
            def writer():
                try:
                    with open(fifo, "wb", buffering=0) as out:
                        out.write(b"x" * (50 << 20))
                except (BrokenPipeError, OSError):
                    pass
            thread = threading.Thread(target=writer, daemon=True)
            thread.start()
            (root / "case.f").write_text('print(open("feed.fifo").length())\n')
            peak, _, err = timed(nift, root, budget)
            thread.join(timeout=5)
            if check("fifo", peak,
                     subprocess.CompletedProcess([], 1 if b"memory budget" in err else 0, b"", err), failures):
                passed += 1

        # ls: 20,000 names, 1 MB budget.
        names = root / "names"
        names.mkdir()
        for i in range(20000):
            (names / f"n{i:05d}").touch()
        (root / "case.f").write_text('l := ls("names"); print(l.length())\n')
        peak, _, err = timed(nift, root, budget)
        if check("ls", peak, subprocess.CompletedProcess([], 1 if b"memory budget" in err else 0, b"", err),
                 failures):
            passed += 1

        # glob: 150,000 matches, 1 MB budget.
        big_dir = root / "big"
        big_dir.mkdir()
        for i in range(150000):
            (big_dir / f"f{i:06d}").touch()
        (root / "case.f").write_text('l := ls("big/*"); print(l.length())\n')
        peak, _, err = timed(nift, root, budget)
        if check("glob", peak, subprocess.CompletedProcess([], 1 if b"memory budget" in err else 0, b"", err),
                 failures):
            passed += 1

        # The same 150,000-match glob with a large budget must succeed.
        allowed = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="1000000000")
        (root / "case.f").write_text('l := ls("big/*"); print(l.length())\n')
        proc = subprocess.run([nift, "case.f"], cwd=root, env=allowed, stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        if proc.returncode != 0 or proc.stdout.strip() != b"150000":
            failures.append("glob-large-budget: expected successful 150000 matches")
        else:
            passed += 1

        # env / getenv: 60 KB value with a 50 KB budget.
        small = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="50000", BIGVAR="x" * 60000)
        for label, body in (("env", "e := env(); print(type(e))\n"),
                            ("getenv", 'v := getenv("BIGVAR"); print(v != null)\n')):
            (root / "case.f").write_text(body)
            peak, _, err = timed(nift, root, small)
            if check(label, peak,
                     subprocess.CompletedProcess([], 1 if b"memory budget" in err else 0, b"", err), failures):
                passed += 1

    for failure in failures:
        print("FAIL", failure)
    print(f"PASS {passed}, FAIL {len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
