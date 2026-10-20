#!/usr/bin/env python3
"""records20k benchmark (integration stage 4; owner's reporting rules): the
suite's `records` workload at n = 5,000, 10,000 and 20,000, both engines
from the same binary.

Per n and engine, `--runs` fresh processes (alternating engines; cold in
the sense of a new process each time, the OS file cache is not dropped):
  * headline: whole-program wall time of the process (startup, parsing,
    compilation and execution included), median and spread (min, max,
    interquartile range);
  * the VM's own timers from NIFT_VM_STATS (parse_us, compile_us,
    execute_us), shown separately; never used for a speed-up claim;
  * identical work: every run's stdout must be byte-identical across the two
    engines and its witness line (n and checksum) equal the suite's
    expectation.
The machine (instance type, CPU, cores), the nift source commit and the
suite's source (workloads.py hash) are printed with the results.

  vm_records20k_bench.py --nift ./nift --suite <suite dir> [--runs 11] [--commit SHA] [--json out]
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vm_records20k import WITNESS, load_workloads  # noqa: E402


def machine():
    def read(path):
        try:
            with open(path) as f:
                return f.read().strip()
        except OSError:
            return "?"
    cpu = "?"
    for line in read("/proc/cpuinfo").splitlines():
        if line.startswith("model name"):
            cpu = line.split(":", 1)[1].strip()
            break
    return {"instance": f"{read('/sys/devices/virtual/dmi/id/sys_vendor')} "
                        f"{read('/sys/devices/virtual/dmi/id/product_name')}",
            "cpu": cpu, "cores": os.cpu_count(), "kernel": platform.release()}


def one(nift, d, vm):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "HOME": d,
           "TMPDIR": d}
    stats = os.path.join(d, ".stats")
    if os.path.exists(stats):
        os.unlink(stats)
    if vm:
        env.update({"NIFT_VM": "require", "NIFT_VM_STATS": stats})
    t0 = time.perf_counter()
    p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL, timeout=600)
    wall = time.perf_counter() - t0
    st = None
    if vm and os.path.exists(stats):
        lines = open(stats).read().splitlines()
        st = json.loads(lines[-1]) if lines else None
    return p, wall, st


def spread(xs):
    xs = sorted(xs)
    q = statistics.quantiles(xs, n=4) if len(xs) >= 2 else [xs[0]] * 3
    return {"median": statistics.median(xs), "min": xs[0], "max": xs[-1], "iqr": q[2] - q[0], "runs": len(xs)}


def fmt(s, scale, unit):
    return (f"median {s['median'] * scale:.2f} {unit} (min {s['min'] * scale:.2f}, max {s['max'] * scale:.2f}, "
            f"IQR {s['iqr'] * scale:.2f}; {s['runs']} runs)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--suite", required=True)
    ap.add_argument("--runs", type=int, default=11)
    ap.add_argument("--commit", default="")
    ap.add_argument("--json")
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    wl = load_workloads(os.path.abspath(a.suite))
    records = wl.W["records"]
    with open(os.path.join(a.suite, "workloads.py"), "rb") as f:
        suite_hash = hashlib.sha256(f.read()).hexdigest()
    report = {"machine": machine(), "nift_commit": a.commit, "suite_workloads_sha256": suite_hash, "sizes": {}}
    print(f"machine: {report['machine']}")
    print(f"nift source commit (both engines, one binary): {a.commit or '?'}; suite workloads.py sha256 {suite_hash}")
    failures = []
    work = tempfile.mkdtemp(prefix="nift-records20k-bench-")
    try:
        for n in (5000, 10000, 20000):
            d = tempfile.mkdtemp(dir=work)
            with open(os.path.join(d, "p.f"), "w") as f:
                f.write(records["nift"].format(N=n))
            want = tuple(records["expect"](n))
            walls = {"legacy": [], "vm": []}
            timers = {"parse_us": [], "compile_us": [], "execute_us": []}
            outputs = set()
            witness = None
            for r in range(a.runs):
                for engine in (("vm", "legacy") if r % 2 else ("legacy", "vm")):
                    p, wall, st = one(nift, d, engine == "vm")
                    m = WITNESS.search(p.stdout.decode("utf-8", "replace"))
                    got = (int(m.group(1)), int(m.group(3))) if m else None
                    if p.returncode != 0 or got != want:
                        failures.append(f"n={n} {engine} run {r}: exit {p.returncode}, witness {got} != {want}")
                    if engine == "vm":
                        if not st or st.get("outcome") != "vm":
                            failures.append(f"n={n} vm run {r}: not entirely on the VM ({st and st.get('reason')})")
                        else:
                            for k in timers:
                                timers[k].append(st[k])
                    outputs.add(hashlib.sha256(p.stdout).hexdigest())
                    witness = m.group(0) if m else witness
                    walls[engine].append(wall)
            if len(outputs) != 1:
                failures.append(f"n={n}: stdout differs between runs or engines ({len(outputs)} distinct)")
            row = {"witness": witness, "stdout_sha256": sorted(outputs),
                   "wall_s": {e: spread(v) for e, v in walls.items()},
                   "vm_timers_us": {k: spread(v) for k, v in timers.items() if v}}
            report["sizes"][n] = row
            print(f"records n={n}: witness {witness}, expected (n, chk) {want}; stdout sha256 {sorted(outputs)[0][:16]} "
                  f"(identical across engines and runs: {len(outputs) == 1})")
            print(f"  whole-program wall, legacy: {fmt(row['wall_s']['legacy'], 1000, 'ms')}")
            print(f"  whole-program wall, VM:     {fmt(row['wall_s']['vm'], 1000, 'ms')}")
            print(f"  whole-program ratio of medians (legacy / VM): "
                  f"{row['wall_s']['legacy']['median'] / row['wall_s']['vm']['median']:.1f}x")
            for k, s in row["vm_timers_us"].items():
                print(f"  VM {k[:-3]} (own timer, not a speed-up measure): {fmt(s, 0.001, 'ms')}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(report, f, indent=1, sort_keys=True)
    for f in failures:
        print("FAIL " + f)
    print(("PASS" if not failures else "FAIL") + f" records20k bench: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
