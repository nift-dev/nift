#!/usr/bin/env python3
"""CP12C Slice 2 default `for`: memory and per-iteration cost.

  for_s2_bench.py --nift ./nift [--runs 5]

The 1,000,000-element array is built once by the VM's `range` native. The
construction time (base.f) is measured separately and subtracted from each
loop. Memory checks that the loop adds no owning copy: under 18 MB the write
loop succeeds and its heap_peak stays at the array's baseline footprint. A
second copy would add ~16.8 MB and fail the budget.

The 10 % performance objective is reported, not silently waived: a ratio above
1.10 prints MISS and is left for the owner. `--strict` exits 1 on a miss.
"""
import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "for-s2-bench")

BODIES = [("minimal scalar", "min_for.f", "min_while.f"),
          ("arithmetic", "arith_for.f", "arith_while.f"),
          ("modify element", "write_for.f", "write_while.f")]


def run_once(nift, work, name, budget, stats_name):
    d = tempfile.mkdtemp(dir=work, prefix=name + "-")
    shutil.copy(os.path.join(FIXTURES, name), os.path.join(d, name))
    stats = os.path.join(d, stats_name)
    time_file = os.path.join(d, "time.txt")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d,
           "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": "require",
           "NIFT_VM_STATS": stats, "NIFT_VM_HEAP_BUDGET": str(budget)}
    try:
        p = subprocess.run(["/usr/bin/time", "-f", "%M", "-o", time_file, nift, name], cwd=d,
                           env=env, stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
    except subprocess.TimeoutExpired:
        return None, None, 124, None
    st = None
    if os.path.exists(stats):
        try:
            st = json.loads(open(stats).read().splitlines()[-1])
        except (OSError, IndexError, ValueError):
            st = None
    rss = None
    if os.path.exists(time_file):
        m = re.search(r"(\d+)", open(time_file).read())
        if m:
            rss = int(m.group(1))
    return st, p.returncode, p.returncode, rss


def median_runs(nift, work, name, budget, runs, base_name):
    values, peaks, rss = [], [], []
    for k in range(runs):
        st, code, _, r = run_once(nift, work, name, budget, f"stats-{base_name}-{k}.json")
        if code != 0 or not st or st.get("outcome") != "vm":
            return None
        values.append(st.get("execute_us", 0.0))
        peaks.append(st.get("heap_peak", 0))
        if r is not None:
            rss.append(r)
    return {
        "execute_us": statistics.median(values),
        "heap_peak": max(peaks),
        "rss_kb": statistics.median(rss) if rss else None,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--budget", type=int, default=24000000)
    ap.add_argument("--strict", action="store_true")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = 0
    with tempfile.TemporaryDirectory(prefix="for-s2-bench-") as work:
        base = median_runs(nift, work, "base.f", args.budget, args.runs, "base")
        if base is None:
            print("FAIL: base.f did not run on the VM")
            return 1
        print(f"array construction: execute={base['execute_us']/1000:.2f} ms "
              f"heap_peak={base['heap_peak']} rss={base['rss_kb']} kB")
        miss = False
        for label, for_name, while_name in BODIES:
            f = median_runs(nift, work, for_name, args.budget, args.runs, for_name)
            w = median_runs(nift, work, while_name, args.budget, args.runs, while_name)
            if f is None or w is None:
                print(f"FAIL: {label}: a loop did not run on the VM")
                failures += 1
                continue
            fu = f["execute_us"] - base["execute_us"]
            wu = w["execute_us"] - base["execute_us"]
            ratio = fu / wu if wu > 0 else float("inf")
            state = "MISS" if ratio > 1.10 else "ok"
            if state == "MISS":
                miss = True
            print(f"{label:16} for={fu/1000:7.2f} ms  while={wu/1000:7.2f} ms  "
                  f"ratio={ratio:5.3f}  {state}  heap_peak={f['heap_peak']} rss={f['rss_kb']} kB")

        # Memory: the write loop must not copy the array. A copy would roughly
        # double heap_peak and fail an 18 MB budget.
        print("memory (write loop, no copy):")
        for budget in (8000000, 17000000, 18000000, 24000000):
            st, code, _, rss = run_once(nift, work, "write_for.f", budget, f"mem-{budget}.json")
            outcome = (st or {}).get("outcome")
            peak = (st or {}).get("heap_peak")
            live = (st or {}).get("heap_live")
            if budget == 8000000:
                print(f"  budget={budget:>9}  outcome={outcome} reason={(st or {}).get('reason','')} "
                      f"(array alone is {base['heap_peak']} bytes; expected)")
            else:
                ok = code == 0 and outcome == "vm" and peak is not None and peak - base["heap_peak"] <= 65536
                print(f"  budget={budget:>9}  outcome={outcome} heap_peak={peak} live={live} "
                      f"rss={rss} kB  {'ok' if ok else 'FAIL'}")
                if not ok:
                    failures += 1
        print(f"performance objective: {'MISS (see ratios)' if miss else 'PASS (within 10%)'}")
    if failures:
        return 1
    if miss and args.strict:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
