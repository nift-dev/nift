#!/usr/bin/env python3
"""Focused Host-result memory contract check (CP16 N3a/N3b).

Small budgets force open(), unknown-size reads (/dev/stdin and a FIFO), ls,
glob and env/getenv to stop before materialising their full result. Peak RSS is
checked for each case so a late check is visible even when it errors.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


from rss_measure import peak_rss_bytes, time_args


def run_case(command, cwd, env, input_bytes):
    return subprocess.run(command, cwd=cwd, env=env, input=input_bytes,
                          stdin=subprocess.DEVNULL if input_bytes is None else None,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)


def timed(nift, cwd, env, input_bytes=None):
    timing = shutil.which("/usr/bin/time")
    darwin = sys.platform == "darwin"
    proc = run_case([timing, *time_args(darwin), nift, "case.f"], cwd, env, input_bytes)
    rss = peak_rss_bytes(proc.stderr, darwin)
    return ((rss + 1023) // 1024 if rss is not None else None), proc


def build_peak_tracker(root, failures):
    darwin = sys.platform == "darwin"
    source = Path(__file__).with_name("cp16_peak_darwin.c" if darwin else "cp16_peak.c")
    target = root / ("peak.dylib" if darwin else "peak.so")
    compiler = shutil.which("cc") or shutil.which("gcc")
    if not compiler:
        failures.append("peak tracker compiler unavailable")
        return {}
    options = ["-dynamiclib"] if darwin else ["-shared", "-fPIC"]
    libraries = [] if darwin or sys.platform.startswith("freebsd") else ["-ldl"]
    build = subprocess.run([compiler, *options, "-O2", "-o", str(target), str(source), *libraries],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if build.returncode != 0:
        failures.append(f"peak tracker build failed: {build.stderr[-300:]!r}")
        return {}
    return {"DYLD_INSERT_LIBRARIES" if darwin else "LD_PRELOAD": str(target)}


def check(label, peak, proc, failures):
    if proc.returncode != 1 or b"memory budget" not in proc.stderr:
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
    if proc.returncode != 0 or proc.stdout.strip() != expected:
        failures.append(f"{label}: expected successful output {expected!r}")
        return False
    if peak is None or peak > 72 * 1024:
        failures.append(f"{label}: peak RSS {peak} KiB > 72 MiB")
        return False
    print(f"PEAK {label} {peak} KiB")
    return True


def check_pair(label, peak, proc, expected, failures, max_kib=72 * 1024):
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
            print("FAIL vm_host_memory: /usr/bin/time unavailable")
            return 1

        # open: 60 MB regular file, 1 MB budget.
        with open(root / "big.bin", "wb") as handle:
            handle.truncate(60 << 20)
        (root / "case.f").write_text('print(open("big.bin").length())\n')
        peak, proc = timed(nift, root, budget)
        if check("open", peak, proc,
                 failures):
            passed += 1

        # Unknown-size read: 50 MB on /dev/stdin.
        (root / "case.f").write_text('print(open("/dev/stdin").length())\n')
        peak, proc = timed(nift, root, budget, input_bytes=b"x" * (50 << 20))
        if check("stdin", peak, proc,
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
            peak, proc = timed(nift, root, budget)
            thread.join(timeout=5)
            if check("fifo", peak,
                     proc, failures):
                passed += 1

        # R5a: unknown-size streams are capped at 9/17 of the headroom, so
        # 50 MiB under 60 MB is refused cleanly (deliberate contract change).
        large = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="60000000")
        (root / "case.f").write_text('print(open("/dev/stdin").length())\n')
        peak, proc = timed(nift, root, large, input_bytes=b"x" * (50 << 20))
        if proc.returncode == 1 and b"memory budget" in proc.stderr and peak is not None and peak <= 72 * 1024:
            print(f"PEAK stdin-50m-refused {peak} KiB")
            passed += 1
        else:
            failures.append(f"stdin-50m-refused: expected refusal, peak={peak} err={proc.stderr!r}")

        if hasattr(os, "mkfifo"):
            fifo2 = root / "refuse.fifo"
            os.mkfifo(fifo2)
            def writer2():
                try:
                    with open(fifo2, "wb", buffering=0) as out_file:
                        out_file.write(b"x" * (50 << 20))
                except (BrokenPipeError, OSError):
                    pass
            thread2 = threading.Thread(target=writer2, daemon=True)
            thread2.start()
            (root / "case.f").write_text('print(open("refuse.fifo").length())\n')
            peak, proc = timed(nift, root, large)
            thread2.join(timeout=5)
            if proc.returncode == 1 and b"memory budget" in proc.stderr and peak is not None and peak <= 72 * 1024:
                print(f"PEAK fifo-50m-refused {peak} KiB")
                passed += 1
            else:
                failures.append(f"fifo-50m-refused: expected refusal, peak={peak} err={proc.stderr!r}")

        # R5a: a known-size regular file keeps the single exact allocation
        # path, so a 50 MiB file still succeeds under the same 60 MB budget.
        with open(root / "known50.bin", "wb") as handle:
            handle.truncate(50 << 20)
        (root / "case.f").write_text('print(open("known50.bin").length())\n')
        peak, proc = timed(nift, root, large)
        out, err = proc.stdout, proc.stderr
        if proc.returncode == 0 and out.strip() == b"52428800" and peak is not None and peak <= 72 * 1024:
            print(f"PEAK known50-success {peak} KiB")
            passed += 1
        else:
            failures.append(f"known50-success: out={out!r} peak={peak} err={proc.stderr!r}")

        # R4a: small unknown-size stream then a 20 MiB regular read, 60 MB
        # budget. The small result must not retain the whole budget headroom.
        with open(root / "twenty.bin", "wb") as handle:
            handle.truncate(20 << 20)
        (root / "case.f").write_text(
            's := open("/dev/stdin"); t := open("twenty.bin"); print(s.length()); print(t.length())\n')
        for kib in (65, 100, 1024, 3072):
            peak, proc = timed(nift, root, large, input_bytes=b"x" * (kib << 10))
            out, err = proc.stdout, proc.stderr
            if check_pair(f"stream-{kib}k-then-20m", peak,
                          proc,
                          f"{kib << 10}\n20971520", failures):
                passed += 1

        # R5a capacity: a 10 MiB stream result must finalise to a bounded
        # retained size. NIFT_VM_STATS reports the live heap after execution.
        (root / "case.f").write_text('s := open("/dev/stdin"); print(s.length())\n')
        cap_stats = root / "cap-stats.jsonl"
        cap_env = dict(large, NIFT_VM_STATS=str(cap_stats))
        peak, proc = timed(nift, root, cap_env, input_bytes=b"x" * (10 << 20))
        out, err = proc.stdout, proc.stderr
        stats = last_stats(cap_stats)
        if proc.returncode == 0 and out.strip() == b"10485760" and stats and stats.get("heap_bytes", 1 << 60) <= (12 << 20):
            print(f"PEAK stream-10m-capacity {peak} KiB heap={stats.get('heap_bytes')}")
            passed += 1
        else:
            failures.append(f"stream-10m-capacity: output={out!r} stats={stats}")

        # R5a trim charge: the finalisation charge is capacity + size, not
        # 2 * size (mutant 10 restores the undercharge and is caught here).
        (root / "case.f").write_text('s := open("/dev/stdin"); print(s.length())\n')
        probe_env = dict(large, NIFT_HOST_TRIM_PROBE="1")
        _, proc = timed(nift, root, probe_env, input_bytes=b"x" * (6 << 20))
        out, err = proc.stdout, proc.stderr
        match = re.search(rb"NIFT_TRIM charge=(\d+) capacity=(\d+) size=(\d+)", err)
        if proc.returncode == 0 and match and int(match.group(1)) == int(match.group(2)) + int(match.group(3)) and out.strip() == b"6291456":
            passed += 1
        else:
            failures.append(f"stream-trim-charge: out={out!r} probe={match.group(0) if match else None}")

        # R4a: a 1 KiB stream under a 1 GiB budget still succeeds.
        huge = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="1000000000")
        (root / "case.f").write_text('s := open("/dev/stdin"); print(s.length())\n')
        peak, proc = timed(nift, root, huge, input_bytes=b"x" * (1 << 10))
        out, err = proc.stdout, proc.stderr
        if proc.returncode == 0 and out.strip() == b"1024":
            passed += 1
        else:
            failures.append(f"stream-1k-1g: output={out!r} err={err!r}")

        # R5a/R6 monotonicity, in 1 MiB steps (R5 tested 2/6/10/20/25/30 and
        # missed a 26-28 MiB refusal gap caused by std::string::reserve
        # rounding past the 9/17 cap). Under 60 MB, 1-30 MiB must succeed and
        # 31-34 MiB must refuse cleanly; any success after a refusal fails.
        # Each run also records the native large-block peak when the tracker
        # builds (it must stay within the budget plus 96 KiB).
        tracker = build_peak_tracker(root, failures)
        (root / "case.f").write_text('s := open("/dev/stdin"); print(s.length())\n')
        sweep_env = dict(large, **tracker)
        refused_at = None
        for mib in range(1, 35):
            proc = run_case([nift, "case.f"], root, sweep_env, b"x" * (mib << 20))
            out, err = proc.stdout, proc.stderr
            ok = proc.returncode == 0 and out.strip() == str(mib << 20).encode()
            refused = proc.returncode == 1 and not out.strip() and b"memory budget" in err
            match = re.search(rb"PEAK_LARGE_BYTES (\d+)", err)
            peak_large = int(match.group(1)) if match else None
            if tracker and peak_large is None:
                failures.append(f"stream-monotonic-{mib}m: native peak was not measured")
            elif peak_large is not None and peak_large > 60000000 + (96 << 10):
                failures.append(f"stream-monotonic-{mib}m: native peak {peak_large} exceeds the budget")
            if ok and refused_at is not None:
                failures.append(f"stream-monotonic-{mib}m: succeeds after a refusal at {refused_at} MiB")
            elif mib <= 30 and not ok:
                failures.append(f"stream-monotonic-{mib}m: expected success, out={out[:60]!r} err={err[-200:]!r}")
            elif mib > 30 and not refused:
                failures.append(f"stream-monotonic-{mib}m: expected a clean refusal, out={out[:60]!r} err={err[-200:]!r}")
            else:
                passed += 1
            if refused and refused_at is None:
                refused_at = mib
        print(f"PEAK stream-monotonic first refusal at {refused_at} MiB (tracker {'on' if tracker else 'off'})")

        # R5a peak tracker: native large-block peak must stay within the
        # budget plus 96 KiB. The control (`print(1)`) establishes the
        # interpreter's own large-block baseline.
        if tracker:
            def peak_run(body, input_bytes=None):
                (root / "case.f").write_text(body)
                proc = run_case([nift, "case.f"], root, sweep_env, input_bytes)
                match = re.search(rb"PEAK_LARGE_BYTES (\d+)", proc.stderr)
                return int(match.group(1)) if match and proc.returncode == 0 else None

            baseline_peak = peak_run('print(1)\n')
            for mib in (6, 10, 20, 30):
                got = peak_run('s := open("/dev/stdin"); print(s.length())\n', b"x" * (mib << 20))
                limit = 60000000 + (96 << 10)
                if baseline_peak is not None and got is not None and (mib << 20) <= got <= limit:
                    print(f"PEAK stream-tracker-{mib}m {got} <= {limit}")
                    passed += 1
                else:
                    failures.append(f"stream-tracker-{mib}m: peak={got} baseline={baseline_peak} limit={limit}")

        # ls: 20,000 names, 1 MB budget.
        names = root / "names"
        names.mkdir()
        for i in range(20000):
            (names / f"n{i:05d}").touch()
        (root / "case.f").write_text('l := native::ls("names"); print(l.length())\n')
        peak, proc = timed(nift, root, budget)
        if check("ls", peak, proc,
                 failures):
            passed += 1

        # glob: 150,000 matches, 1 MB budget.
        big_dir = root / "deep" / "a" / "b" / "c" / "d" / "e" / "f" / "g" / "h"
        big_dir.mkdir(parents=True)
        for i in range(150000):
            (big_dir / f"f{i:06d}").touch()
        deep_glob = 'l := native::ls("deep/a/b/c/d/e/f/g/h/*"); print(l.length())\n'
        (root / "case.f").write_text(deep_glob)
        peak, proc = timed(nift, root, budget)
        if check("glob", peak, proc,
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
        base_peak, base = timed(nift, root, base_env)
        if base.returncode != 0 or base.stdout.strip() != b"1" or base_peak is None:
            failures.append("glob baseline: expected successful output and peak RSS")
        tolerance = 8 << 10  # allocator/stdio tolerance, stated in the report
        for budget_bytes in (36_000_000, 40_000_000, 60_000_000):
            env = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET=str(budget_bytes))
            stats_path = root / f"glob-{budget_bytes}.jsonl"
            env["NIFT_VM_STATS"] = str(stats_path)
            (root / "case.f").write_text(deep_glob)
            peak, proc = timed(nift, root, env)
            out, err = proc.stdout, proc.stderr
            stats = last_stats(stats_path)
            bound = (base_peak or 0) + budget_bytes // 1024 + (96 + tolerance)
            if budget_bytes == 36_000_000:
                if proc.returncode == 0 and out.strip() == b"150000" and stats and stats.get("heap_bytes", 1 << 60) <= budget_bytes:
                    print(f"GLOB {budget_bytes} success heap={stats.get('heap_bytes')} peak={peak} KiB")
                    passed += 1
                else:
                    failures.append(f"glob-deep-{budget_bytes}: required success, out={out!r} stats={stats} err={err!r}")
            elif proc.returncode == 0 and out.strip() == b"150000" and stats and stats.get("heap_bytes", 1 << 60) <= budget_bytes:
                print(f"GLOB {budget_bytes} success heap={stats.get('heap_bytes')} peak={peak} KiB")
                passed += 1
            elif proc.returncode == 1 and b"memory budget" in err and peak is not None and peak <= bound:
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
        (root / "case.f").write_text('l := native::ls("dup/**/**/*"); print(l.length())\n')
        dup_low = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="600000")
        peak, proc = timed(nift, root, dup_low)
        if check("glob-duplicate-low", peak,
                 proc, failures):
            passed += 1
        dup_ok = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="2000000")
        peak, proc = timed(nift, root, dup_ok)
        out, err = proc.stdout, proc.stderr
        if check_success("glob-duplicate-ok", peak,
                         proc,
                         b"1005", failures):
            passed += 1

        # env / getenv: 60 KB value with a 50 KB budget.
        small = dict(os.environ, NIFT_VM="require", NIFT_VM_HEAP_BUDGET="50000", BIGVAR="x" * 60000)
        for label, body in (("env", "e := env(); print(type(e))\n"),
                            ("getenv", 'v := getenv("BIGVAR"); print(v != null)\n')):
            (root / "case.f").write_text(body)
            peak, proc = timed(nift, root, small)
            if check(label, peak,
                     proc, failures):
                passed += 1

    for failure in failures:
        print("FAIL", failure)
    print(f"PASS {passed}, FAIL {len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
