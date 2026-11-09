#!/usr/bin/env python3
"""CP12C Slice 2 default `for`: memory and per-iteration cost.

  for_s2_bench.py --nift ./nift [--runs 5]

The 1,000,000-element array is built once by the VM's `range` native. The
construction time (base.f) is measured separately and subtracted from each
loop. The 10 % performance objective is reported, not silently waived: a ratio
above 1.10 prints MISS and is left for the owner. `--strict` exits 1 on a miss.

Memory (B05, owner approval 2026-11-09 17:31; B5 contract D5, b5-refsem.md 1 and
10.9: every loop over a binding iterates an entry snapshot, an explicit O(n),
budget-checked copy). The bench used to give the `for` loops a 24 MB budget,
room for the array only (before B5 a `for` loop made no copy), and to expect
write_for.f refused by the deleted Phase A guard: both stale. Now:

  snapshot(n) = n * 16 + 64 bytes: the copy's storage as the VM charges it
      (n Values of 16 bytes, plus the vector's fixed part); the reservation
      check before the copy asks for n * 16 + 128 (Vm::reserve_snapshot,
      src/rt/vm_iter.cpp), so a budget stop leaves nothing half built
  for budget = base_peak + (N * 16 + 128) + MARGIN, base_peak the array alone
      (base.f's measured heap_peak), MARGIN = 64 KiB: the program's own small
      values and buffers, the same tolerance the write loop's no-copy check
      uses. One more copy (16 MB) does not fit, so the budget asserts "one
      snapshot", not "some memory".
  checks: each `for` loop's heap_peak = base_peak + snapshot(N) (within TOL,
      256 bytes, the VM's exact accounting); heap_live at exit = base_peak (the snapshot is released);
      three loops in sequence and a looping function called twice keep that
      peak (no accumulation); nested loops hold one snapshot per level (two
      levels over 1,000 elements, three over 100: base + levels * snapshot);
      write_for.f runs on the VM with legacy's output (B5: the deleted guard's
      refusal is gone) within the same peak; a budget of base_peak plus half a
      snapshot stops with the documented heap-budget stop (exit 70, outcome
      internal-error, reason "memory budget exceeded", nothing printed); the
      while loops keep the no-copy check (heap_peak within 64 KiB of the array).
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
          ("arithmetic", "arith_for.f", "arith_while.f")]
N = 1000000
SNAPSHOT_REQUEST = N * 16 + 128   # Vm::reserve_snapshot: n * sizeof(Value) + 128
MARGIN = 65536                    # the program's own small values (see the docstring)
TOL = 256                         # heap_peak tolerance: the VM charges exact sizes (its own accounting,
                                  # not malloc), below the smallest snapshot checked (100 elements: 1,664)


def snapshot(n):
    return n * 16 + 64            # the copy's charged storage (n Values + the vector's fixed part)


def run_once(nift, work, name, budget, stats_name, vm="require", out=None):
    d = tempfile.mkdtemp(dir=work, prefix=name + "-")
    shutil.copy(os.path.join(FIXTURES, name), os.path.join(d, name))
    stats = os.path.join(d, stats_name)
    time_file = os.path.join(d, "time.txt")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d,
           "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": vm,
           "NIFT_VM_STATS": stats, "NIFT_VM_HEAP_BUDGET": str(budget)}
    try:
        p = subprocess.run(["/usr/bin/time", "-f", "%M", "-o", time_file, nift, name], cwd=d,
                           env=env, stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
    except subprocess.TimeoutExpired:
        return None, None, 124, None
    if out is not None:
        out.append((p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")))
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


def memory_case(nift, work, name, budget, want_peak, want_live, label):
    """one run: on the VM, heap_peak = want_peak and heap_live = want_live (within TOL)"""
    st, code, _, _ = run_once(nift, work, name, budget, f"m-{name}.json")
    peak, live = (st or {}).get("heap_peak"), (st or {}).get("heap_live")
    ok = (code == 0 and st and st.get("outcome") == "vm" and peak is not None and live is not None
          and abs(peak - want_peak) <= TOL and abs(live - want_live) <= TOL)
    print(f"  {label:44} heap_peak={peak} (want {want_peak}) live={live} (want {want_live})  {'ok' if ok else 'FAIL'}")
    return ok


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
        base_peak = base["heap_peak"]
        for_budget = base_peak + SNAPSHOT_REQUEST + MARGIN
        print(f"for budget = base_peak {base_peak} + snapshot request {SNAPSHOT_REQUEST} + margin {MARGIN} = {for_budget}")
        miss = False
        for label, for_name, while_name in BODIES:
            f = median_runs(nift, work, for_name, for_budget, args.runs, for_name)
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
        # B05: the snapshot contract (see the docstring)
        print("memory (for loops: one entry snapshot, released at exit):")
        one = base_peak + snapshot(N)
        cases = [("min_for.f", "minimal scalar loop"), ("arith_for.f", "arithmetic loop"),
                 ("seq3_for.f", "three loops in sequence (no accumulation)"),
                 ("fn_for.f", "a looping function called twice (no accumulation)")]
        for name, label in cases:
            failures += 0 if memory_case(nift, work, name, for_budget, one, base_peak, label) else 1
        small = {}
        for name in ("base_1k.f", "base_100.f"):
            st, code, _, _ = run_once(nift, work, name, for_budget, f"b-{name}.json")
            small[name] = (st or {}).get("heap_peak") if code == 0 and st and st.get("outcome") == "vm" else None
        if None in small.values():
            print("FAIL: the small base arrays did not run on the VM")
            failures += 1
        else:
            b1k, b100 = small["base_1k.f"], small["base_100.f"]
            for name, base_small, n, levels, label in (
                    ("loop_1k.f", b1k, 1000, 1, "one level over 1,000 elements"),
                    ("nested2_1k.f", b1k, 1000, 2, "two nested levels over 1,000 (one snapshot each)"),
                    ("nested3_100.f", b100, 100, 3, "three nested levels over 100 (one snapshot each)")):
                ok = memory_case(nift, work, name, for_budget, base_small + levels * snapshot(n), base_small, label)
                failures += 0 if ok else 1
        # write_for.f: B5 deleted the Phase A guard (reference-guard:lent); the loop writes the array it
        # iterates and runs on the VM with legacy's output, within one snapshot
        outs = []
        st, code, _, _ = run_once(nift, work, "write_for.f", for_budget, "w-vm.json", out=outs)
        lst, lcode, _, _ = run_once(nift, work, "write_for.f", for_budget, "w-legacy.json", vm="0", out=outs)
        peak = (st or {}).get("heap_peak")
        ok = (code == 0 and lcode == 0 and st and st.get("outcome") == "vm" and outs[0] == outs[1]
              and peak is not None and abs(peak - one) <= TOL)
        print(f"write_for.f: runs on the VM, legacy's output {outs[0][0].strip()!r} vs {outs[1][0].strip()!r}, "
              f"heap_peak={peak} (want {one}): {'ok' if ok else 'FAIL'}")
        failures += 0 if ok else 1
        # negative: a budget genuinely too small for the snapshot (the array plus half a copy)
        small_budget = base_peak + snapshot(N) // 2
        outs = []
        st, code, _, _ = run_once(nift, work, "min_for.f", small_budget, "neg.json", out=outs)
        ok = (code == 70 and st and st.get("outcome") == "internal-error" and st.get("reason") == "memory budget exceeded"
              and outs[0][0] == "" and "memory budget exceeded" in outs[0][1])
        print(f"budget {small_budget} (array + half a snapshot): exit {code}, outcome {(st or {}).get('outcome')}, "
              f"reason {(st or {}).get('reason')!r}, stdout {outs[0][0]!r}: the documented heap-budget stop "
              f"{'ok' if ok else 'FAIL'}")
        failures += 0 if ok else 1
        # Memory: the write loop must not copy the array. A copy would roughly
        # double heap_peak and fail an 18 MB budget.
        print("memory (write loop, no copy):")
        for budget in (8000000, 17000000, 18000000, 24000000):
            st, code, _, rss = run_once(nift, work, "write_while.f", budget, f"mem-{budget}.json")
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
