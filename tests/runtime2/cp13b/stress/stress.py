#!/usr/bin/env python3
"""CP13B memory stress: 1,000,000 caught exceptions under an 8 MB VM heap.

  stress.py --nift ./nift --prepare     # Phase A: record the current refusal
  stress.py --nift ./nift               # Phase B: account and release checks

For each count (default 1,000 and 1,000,000) the workload is generated from
stress_caught.f.in, run with NIFT_VM=require and NIFT_VM_HEAP_BUDGET=8000000,
and measured two ways:

  * accounted heap: stats `heap_peak` (live rt heap seen by budget checks) and
    `heap_live` at exit. The run must stay under the budget and `heap_live`
    must not grow with the iteration count: peak/live at 1,000,000 must be
    within kSlack bytes of the 1,000-iteration run. Retained exception values
    or handlers would grow linearly and fail this.
  * RSS: ru_maxrss of that exact child process (os.wait4), reported
    separately from the accounted heap.

A resource-limit stop, a refusal, or a non-vm outcome fails the gate. The
unit of the failure is the workload itself, not the harness.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TEMPLATES = {"caught": HERE / "stress_caught.f.in", "caught_cell": HERE / "stress_caught_cell.f.in"}
BUDGET = 8000000
SLACK = 65536


class Run:
    def __init__(self, out, err, rc, stats, rss_kib):
        self.out = out
        self.err = err
        self.rc = rc
        self.stats = stats
        self.rss_kib = rss_kib


def measured(nift, folder, source_name, env):
    stdout = folder / ".stdout"
    stderr = folder / ".stderr"
    with stdout.open("wb") as out, stderr.open("wb") as err:
        proc = subprocess.Popen([nift, source_name], cwd=folder, env=env, stdin=subprocess.DEVNULL,
                                stdout=out, stderr=err)
        try:
            _, status, usage = os.wait4(proc.pid, 0)
        except ChildProcessError:
            proc.wait()
            raise
        proc.returncode = os.waitstatus_to_exitcode(status)
        rss = usage.ru_maxrss
    return stdout.read_bytes().decode(errors="replace"), stderr.read_bytes().decode(errors="replace"), \
        proc.returncode, rss


def run(nift, template, count, mode):
    with tempfile.TemporaryDirectory(prefix="cp13b-stress-") as folder:
        folder = Path(folder)
        text = template.read_text().replace("@COUNT@", str(count))
        (folder / "stress.f").write_text(text)
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": str(folder),
            "TMPDIR": str(folder),
            "LANG": "C",
            "LC_ALL": "C",
            "TZ": "UTC",
            "NIFT_PREVALIDATE": "off",
            "NIFT_VM": mode,
        }
        stats_path = folder / ".stats.jsonl"
        if mode != "0":
            env["NIFT_VM_STATS"] = str(stats_path)
            env["NIFT_VM_HEAP_BUDGET"] = str(BUDGET)
        out, err, rc, rss = measured(nift, folder, "stress.f", env)
        stats = None
        if stats_path.exists():
            lines = stats_path.read_text().splitlines()
            if lines:
                try:
                    stats = json.loads(lines[-1])
                except ValueError:
                    raise SystemExit("stress: malformed NIFT_VM_STATS")
        return Run(out, err, rc, stats, rss)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--counts", default="1000,1000000")
    args = parser.parse_args()
    nift = str(Path(args.nift).resolve())
    counts = [int(value) for value in args.counts.split(",")]
    if args.prepare:
        for workload, template in TEMPLATES.items():
            got = run(nift, template, counts[-1], "require")
            if got.stats is None:
                print(f"FAIL stress prepare {workload}: no NIFT_VM_STATS")
                return 1
            if got.stats.get("outcome") == "require-error":
                print(f"PREPARED stress {workload}: current VM refuses the workload before execution "
                      f"({got.stats.get('reason')}, vm_executions={got.stats.get('vm_executions')}, "
                      f"legacy_executions={got.stats.get('legacy_executions')}); RSS {got.rss_kib} KiB")
            else:
                print(f"stress prepare {workload}: the VM executed the workload "
                      f"(outcome {got.stats.get('outcome')}); running the future checks")
        print("PREPARED stress: accounting/release checks run in Phase B (future mode)")
        return 0
    failures = 0
    for workload, template in TEMPLATES.items():
        legacy = run(nift, template, counts[-1], "0")
        if (legacy.out, legacy.rc) != ("ok\n", 0):
            print(f"FAIL stress {workload}: legacy oracle is {legacy.out!r}/{legacy.rc}, expected 'ok'/0")
            return 1
        results = {}
        for count in counts:
            got = run(nift, template, count, "require")
            if got.stats is None:
                print(f"FAIL stress {workload} {count}: no NIFT_VM_STATS (harness failure, not a detection)")
                return 1
            st = got.stats
            ok = True
            if st.get("outcome") != "vm":
                print(f"FAIL stress {workload} {count}: outcome {st.get('outcome')} ({st.get('reason')})")
                ok = False
            if st.get("vm_executions") != 1:
                print(f"FAIL stress {workload} {count}: vm_executions {st.get('vm_executions')}")
                ok = False
            if (got.out, got.rc) != ("ok\n", 0):
                print(f"FAIL stress {workload} {count}: output {got.out!r}/exit {got.rc}")
                ok = False
            if "heap_peak" not in st or "heap_live" not in st:
                print(f"FAIL stress {workload} {count}: NIFT_VM_STATS is missing heap_peak/heap_live "
                      "(missing evidence, not zero)")
                failures += 1
                continue
            peak = st["heap_peak"]
            live = st["heap_live"]
            if peak > BUDGET:
                print(f"FAIL stress {workload} {count}: accounted peak {peak} exceeds the {BUDGET}-byte budget")
                ok = False
            results[count] = (peak, live, got.rss_kib)
            print(f"stress {workload} {count}: accounted heap_peak={peak} heap_live={live} budget={BUDGET} "
                  f"rss_max_kib={got.rss_kib}")
            failures += 0 if ok else 1
        if len(counts) >= 2:
            small, large = min(counts), max(counts)
            peak_delta = results[large][0] - results[small][0]
            live_delta = results[large][1] - results[small][1]
            print(f"stress {workload} growth {small}->{large}: heap_peak delta {peak_delta}, "
                  f"heap_live delta {live_delta}, slack {SLACK}")
            if peak_delta > SLACK or live_delta > SLACK:
                print(f"FAIL stress {workload}: accounted heap grows with the iteration count "
                      "(retained values, cells or handlers)")
                failures += 1
    print(f"{'PASS' if failures == 0 else 'FAIL'} stress: {len(TEMPLATES)} workload(s), {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
