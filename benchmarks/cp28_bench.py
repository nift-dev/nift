#!/usr/bin/env python3
"""CP28 v4.2 performance/memory certification harness.

Runs the CP28 workload fixtures and reports:
  - correctness (expected output marker per workload)
  - per-workload build --all timings (interleaved baseline/current for v4.1-only
    workloads so noise is paired; candidate-only for v4.2-only workloads)
  - distributions (min/p25/median/p75/max, mean, stddev) and the
    current/baseline median delta for the v4.1-only workloads
  - peak resident memory (via /usr/bin/time -v when available)
"""
import argparse
import json
import os
import pathlib
import shutil
import statistics
import subprocess
import tempfile
import time

import cp28_fixtures


ap = argparse.ArgumentParser()
ap.add_argument("--baseline", required=True, help="pre-v4.2 baseline nift binary")
ap.add_argument("--current", required=True, help="v4.2 candidate nift binary")
ap.add_argument("--samples", type=int, default=15)
ap.add_argument("--warmup", type=int, default=2)
ap.add_argument("--workloads", nargs="*", help="restrict to these workload names")
ap.add_argument("--out", default="", help="JSON report path")
a = ap.parse_args()

TIME_BIN = shutil.which("time") or "/usr/bin/time"


def build(nift: str, root: pathlib.Path, measure_rss: bool = False):
    cmd = [nift, "build", "--all"]
    if measure_rss and TIME_BIN:
        rss_cmd = ["/usr/bin/time", "-v"] if TIME_BIN == "/usr/bin/time" else ["time", "-v"]
        p = subprocess.run(rss_cmd + cmd, cwd=root, stdout=subprocess.DEVNULL,
                           stderr=subprocess.PIPE, text=True)
        rss = None
        for line in p.stderr.splitlines():
            if "Maximum resident set size" in line:
                rss = int(line.split(":")[1].strip())
        return p.returncode, p.stderr, rss
    start = time.perf_counter()
    p = subprocess.run(cmd, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return p.returncode, p.stderr.decode(errors="replace"), time.perf_counter() - start


def verify(nift: str, root: pathlib.Path, marker: str):
    rc, err, _ = build(nift, root)
    if rc:
        return False, f"build failed: {err.strip()[:300]}"
    out = (root / "public" / "index.html").read_text(errors="replace")
    if marker not in out:
        return False, f"expected marker {marker!r} not found in output"
    return True, "ok"


def report(name, samples, base_median=None, current_median=None):
    s = sorted(samples)
    mean = statistics.mean(s)
    var = sum((x - mean) ** 2 for x in s) / (len(s) - 1) if len(s) > 1 else 0.0
    p25 = s[len(s) // 4]
    p75 = s[(3 * len(s)) // 4]
    return {
        "n": len(s),
        "min": round(s[0], 6),
        "p25": round(p25, 6),
        "median": round(statistics.median(s), 6),
        "p75": round(p75, 6),
        "max": round(s[-1], 6),
        "mean": round(mean, 6),
        "stddev": round(var ** 0.5, 6),
        "raw": [round(x, 6) for x in s],
    }


results = {}
for name in cp28_fixtures.WORKLOAD_DEFS:
    if a.workloads and name not in a.workloads:
        continue
    is_v41 = cp28_fixtures.WORKLOAD_DEFS[name]["v41"]
    with tempfile.TemporaryDirectory(prefix=f"cp28-{name}-") as td:
        root = pathlib.Path(td) / "w"
        marker = cp28_fixtures.make_workload(name, root)

        ok, msg = verify(a.current, root, marker)
        if not ok:
            results[name] = {"status": "FAIL", "reason": msg}
            print(f"FAIL  {name}: {msg}")
            continue

        cur_samples = []
        base_samples = []
        rss_cur = None
        for _ in range(a.warmup):
            if is_v41:
                build(a.baseline, root)
            build(a.current, root)
        for _ in range(a.samples):
            if is_v41:
                rc, err, t = build(a.baseline, root)
                if rc:
                    results[name] = {"status": "FAIL", "reason": err.strip()[:200]}
                    break
                base_samples.append(t)
            rc, err, t = build(a.current, root)
            if rc:
                results[name] = {"status": "FAIL", "reason": err.strip()[:200]}
                break
            cur_samples.append(t)
            if rss_cur is None:
                _, _, rss_cur = build(a.current, root, measure_rss=True)

        if name in results and results[name].get("status") == "FAIL":
            continue

        entry = {"status": "PASS", "v41": is_v41, "marker": marker,
                 "current": report(name, cur_samples)}
        if is_v41:
            entry["baseline"] = report(name, base_samples)
            bm = entry["baseline"]["median"]
            cm = entry["current"]["median"]
            entry["delta"] = {"current/baseline_median": round(cm / bm, 4) if bm else None,
                              "pct": round((cm - bm) / bm * 100, 2) if bm else None}
        if rss_cur is not None:
            entry["peak_rss_kb"] = rss_cur
        results[name] = entry
        line = f"PASS  {name:<14} current={entry['current']['median']:.6f}s"
        if is_v41:
            line += f"  baseline={entry['baseline']['median']:.6f}s  delta={entry['delta']['pct']:+.2f}%"
        if rss_cur is not None:
            line += f"  rss={rss_cur/1024:.1f}MB"
        print(line)

if a.out:
    pathlib.Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(a.out).write_text(json.dumps(results, indent=2))
print("done")