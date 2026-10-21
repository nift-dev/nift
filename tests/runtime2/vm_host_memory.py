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


def check_success(label, peak, proc, expected, failures):
    if proc is None:
        failures.append(f"{label}: /usr/bin/time unavailable")
        return False
    if proc.returncode != 0 or proc.stdout.strip() != expected:
        failures.append(f"{label}: expected successful output {expected!r}")
        return False
    if peak is None or peak > 72 * 1024:
        failures.append(f"{label}: peak RSS {peak} KiB > 72 MiB")
        return False
    print(f"PEAK {label} {peak} KiB")
    return True


def check_pair(label, peak, proc, expected, failures, max_kib=72 * 1024):
    if proc is None:
        failures.append(f"{label}: /usr/bin/time unavailable")
        return False
    if proc.returncode != 0 or proc.stdout.decode(errors="replace").strip() != expected:
        failures.append(f"{label}: expected output {expected!r}, got {proc.stdout!r} rc={proc.returncode}")
        return False
    if peak is None or peak > max_kib:
        failures.append(f"{label}: peak RSS {peak} KiB > {max_kib} KiB")
        return False
    print(f"PEAK {label} {peak} KiB")
    return True


def last_stats(path):
    if not path.exists():
        return None
    lines = [line for line in path.read_text().splitlines() if line.strip()]
    if not lines:
        return None
    try:
        import json
        return json.loads(lines[-1])
    except Exception:
        return None


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

        # R1 successful stream reads: 50 MiB with a 60 MB budget.
        large = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="60000000")
        (root / "case.f").write_text('print(open("/dev/stdin").length())\n')
        peak, out, err = timed(nift, root, large, input_bytes=b"x" * (50 << 20))
        if check_success("stdin-success", peak,
                         subprocess.CompletedProcess([], 0 if out.strip() == b"52428800" else 1, out, err),
                         b"52428800", failures):
            passed += 1

        if hasattr(os, "mkfifo"):
            fifo2 = root / "success.fifo"
            os.mkfifo(fifo2)
            def writer2():
                try:
                    with open(fifo2, "wb", buffering=0) as out_file:
                        out_file.write(b"x" * (50 << 20))
                except (BrokenPipeError, OSError):
                    pass
            thread2 = threading.Thread(target=writer2, daemon=True)
            thread2.start()
            (root / "case.f").write_text('print(open("success.fifo").length())\n')
            peak, out, err = timed(nift, root, large)
            thread2.join(timeout=5)
            if check_success("fifo-success", peak,
                             subprocess.CompletedProcess([], 0 if out.strip() == b"52428800" else 1, out, err),
                             b"52428800", failures):
                passed += 1

        # R4a: small unknown-size stream then a 20 MiB regular read, 60 MB
        # budget. The small result must not retain the whole budget headroom.
        with open(root / "twenty.bin", "wb") as handle:
            handle.truncate(20 << 20)
        (root / "case.f").write_text(
            's := open("/dev/stdin"); t := open("twenty.bin"); print(s.length()); print(t.length())\n')
        for kib in (65, 100, 1024, 3072):
            peak, out, err = timed(nift, root, large, input_bytes=b"x" * (kib << 10))
            if check_pair(f"stream-{kib}k-then-20m", peak,
                          subprocess.CompletedProcess([], 0 if out else 1, out, err),
                          f"{kib << 10}\n20971520", failures):
                passed += 1

        # R4a capacity: a 3 MiB stream result must finalise to a bounded
        # retained size. NIFT_VM_STATS reports the live heap after execution.
        (root / "case.f").write_text('s := open("/dev/stdin"); print(s.length())\n')
        cap_stats = root / "cap-stats.jsonl"
        cap_env = dict(large, NIFT_VM_STATS=str(cap_stats))
        peak, out, err = timed(nift, root, cap_env, input_bytes=b"x" * (3 << 20))
        stats = last_stats(cap_stats)
        if out.strip() == b"3145728" and stats and stats.get("heap_bytes", 1 << 60) <= (4 << 20):
            print(f"PEAK stream-3m-capacity {peak} KiB heap={stats.get('heap_bytes')}")
            passed += 1
        else:
            failures.append(f"stream-3m-capacity: output={out!r} stats={stats}")

        # R4a: a 1 KiB stream under a 1 GiB budget still succeeds.
        huge = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="1000000000")
        (root / "case.f").write_text('s := open("/dev/stdin"); print(s.length())\n')
        peak, out, err = timed(nift, root, huge, input_bytes=b"x" * (1 << 10))
        if out.strip() == b"1024":
            passed += 1
        else:
            failures.append(f"stream-1k-1g: output={out!r} err={err!r}")

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
        big_dir = root / "deep" / "a" / "b" / "c" / "d" / "e" / "f" / "g" / "h"
        big_dir.mkdir(parents=True)
        for i in range(150000):
            (big_dir / f"f{i:06d}").touch()
        deep_glob = 'l := ls("deep/a/b/c/d/e/f/g/h/*"); print(l.length())\n'
        (root / "case.f").write_text(deep_glob)
        peak, _, err = timed(nift, root, budget)
        if check("glob", peak, subprocess.CompletedProcess([], 1 if b"memory budget" in err else 0, b"", err),
                 failures):
            passed += 1

        # The same 150,000-match glob with a large budget must succeed.
        allowed = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="1000000000")
        (root / "case.f").write_text(deep_glob)
        proc = subprocess.run([nift, "case.f"], cwd=root, env=allowed, stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        if proc.returncode != 0 or proc.stdout.strip() != b"150000":
            failures.append("glob-large-budget: expected successful 150000 matches")
        else:
            passed += 1

        # R4b: deep-path glob. 36 MB must succeed (the accounting threshold);
        # 40 MB and 60 MB may succeed with the accounted heap inside the budget
        # and RSS under the bound, or refuse before RSS exceeds baseline +
        # budget + 96 KiB + tolerance.
        base_env = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="60000000")
        (root / "case.f").write_text('print(1)\n')
        base_peak, _, _ = timed(nift, root, base_env)
        tolerance = 8 << 10  # allocator/stdio tolerance, stated in the report
        for budget_bytes in (36_000_000, 40_000_000, 60_000_000):
            env = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET=str(budget_bytes))
            stats_path = root / f"glob-{budget_bytes}.jsonl"
            env["NIFT_VM_STATS"] = str(stats_path)
            (root / "case.f").write_text(deep_glob)
            peak, out, err = timed(nift, root, env)
            stats = last_stats(stats_path)
            bound = (base_peak or 0) + budget_bytes // 1024 + (96 + tolerance)
            if budget_bytes == 36_000_000:
                if out.strip() == b"150000" and stats and stats.get("heap_bytes", 1 << 60) <= budget_bytes:
                    print(f"GLOB {budget_bytes} success heap={stats.get('heap_bytes')} peak={peak} KiB")
                    passed += 1
                else:
                    failures.append(f"glob-deep-{budget_bytes}: required success, out={out!r} stats={stats} err={err!r}")
            elif out.strip() == b"150000" and stats and stats.get("heap_bytes", 1 << 60) <= budget_bytes:
                print(f"GLOB {budget_bytes} success heap={stats.get('heap_bytes')} peak={peak} KiB")
                passed += 1
            elif b"memory budget" in err and peak is not None and peak <= bound:
                print(f"GLOB {budget_bytes} refused peak={peak} KiB bound={bound} KiB")
                passed += 1
            else:
                failures.append(f"glob-deep-{budget_bytes}: out={out!r} peak={peak} bound={bound} err={err!r}")

        # R2 duplicate-heavy: the same paths are visited ~6x, but the final
        # unique result is much smaller. A budget between the two rejects.
        dup = root / "dup"
        dup_leaf = dup / "a" / "b" / "c" / "d" / "e"
        dup_leaf.mkdir(parents=True)
        for i in range(1000):
            (dup_leaf / f"f{i:04d}").touch()
        (root / "case.f").write_text('l := ls("dup/**/**/*"); print(l.length())\n')
        dup_low = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="600000")
        peak, _, err = timed(nift, root, dup_low)
        if check("glob-duplicate-low", peak,
                 subprocess.CompletedProcess([], 1 if b"memory budget" in err else 0, b"", err), failures):
            passed += 1
        dup_ok = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="700000")
        peak, out, err = timed(nift, root, dup_ok)
        if check_success("glob-duplicate-ok", peak,
                         subprocess.CompletedProcess([], 0 if out.strip() == b"1005" else 1, out, err),
                         b"1005", failures):
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
