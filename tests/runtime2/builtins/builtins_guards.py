#!/usr/bin/env python3
"""CP14 built-ins: per-native instruction guards (callgrind, D-005).

Runs tests/rt/builtins_bench.cpp under callgrind at several input sizes, one
valgrind process per size, sequentially. For each workload it reports the
instructions per call at every size, the scaling exponent between the two
largest sizes (log(Ir2/Ir1)/log(n2/n1): ~1 linear, ~0 constant; an accidental
O(n^2) shows as ~2), the allocations per call (operator new) and their
exponent, and the live-heap delta per call (exact heap accounting).

  builtins_guards.py <builtins-bench> [--sizes 1,100,10000,100000]
                     [--write GUARDS.json] [--check GUARDS.json] [--markdown]

--write records each workload's Ir per call at every size (the guard; the
check allows +10 %) and the expected exponent class; --check fails when a
measurement exceeds its guard by more than 10 % or an exponent grows by more
than 0.15.
"""
import argparse
import glob
import json
import math
import os
import re
import subprocess
import sys
import tempfile

TOLERANCE = 1.10
EXPONENT_SLACK = 0.15


def measure(bench, n, workdir):
    for old in glob.glob(os.path.join(workdir, "cg.*")):
        os.remove(old)
    out = subprocess.run(["valgrind", "--tool=callgrind", "--callgrind-out-file=" + os.path.join(workdir, "cg.%p"),
                          bench, str(n)], capture_output=True, text=True, check=True)
    meta = {}
    for line in out.stdout.splitlines():
        name, size, reps, allocs, nbytes, delta = line.split()
        meta[name] = {"reps": int(reps), "allocs": int(allocs), "bytes": int(nbytes), "heap": int(delta)}
    ir = {}
    for path in glob.glob(os.path.join(workdir, "cg.*")):
        name = total = None
        with open(path) as handle:
            for line in handle:
                m = re.match(r"desc: Trigger: Client Request: (\S+)", line)
                if m:
                    name = m.group(1)
                m = re.match(r"(?:summary|totals): (\d+)", line)
                if m:
                    total = int(m.group(1))
        if name and total is not None:
            ir[name] = total
    result = {}
    for name, info in meta.items():
        reps = info["reps"]
        result[name] = {"ir": ir[name] / reps, "allocs": info["allocs"] / reps, "bytes": info["bytes"] / reps,
                        "heap": info["heap"] / reps}
    return result


def exponent(a, b, n1, n2):
    if a <= 0 or b <= 0:
        return 0.0
    return math.log(b / a) / math.log(n2 / n1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("bench")
    parser.add_argument("--sizes", default="1,100,1000,10000,100000")
    parser.add_argument("--write")
    parser.add_argument("--check")
    parser.add_argument("--markdown", action="store_true")
    args = parser.parse_args()
    sizes = [int(x) for x in args.sizes.split(",")]
    data = {}
    with tempfile.TemporaryDirectory(prefix="cp14-guards-") as workdir:
        for n in sizes:
            for name, values in measure(os.path.abspath(args.bench), n, workdir).items():
                data.setdefault(name, {})[n] = values
    rows = {}
    for name, per in data.items():
        # Workloads with a size limit (O(n^2) ones) use their two largest sizes.
        have = [n for n in sizes if n in per]
        n1, n2 = have[-2], have[-1]
        rows[name] = {
            "ir": {str(n): round(per[n]["ir"], 1) for n in have},
            "allocs": {str(n): round(per[n]["allocs"], 3) for n in have},
            "heap": {str(n): round(per[n]["heap"], 1) for n in have},
            "exponent": round(exponent(per[n1]["ir"], per[n2]["ir"], n1, n2), 3),
            "alloc_exponent": round(exponent(per[n1]["allocs"] or 1, per[n2]["allocs"] or 1, n1, n2), 3),
            "exponent_sizes": [n1, n2],
        }
    if args.markdown:
        head = " | ".join("Ir/call n=%d" % n for n in sizes)
        print("| workload | %s | exp. (sizes) | allocs/call at those sizes | alloc exp. |" % head)
        print("|---|" + "---:|" * (len(sizes) + 3))
        for name in sorted(rows):
            r = rows[name]
            a, b = r["exponent_sizes"]
            print("| %s | %s | %.2f (%d→%d) | %s / %s | %.2f |" % (
                name, " | ".join("{:,.0f}".format(r["ir"][str(n)]) if str(n) in r["ir"] else "—" for n in sizes),
                r["exponent"], a, b, r["allocs"][str(a)], r["allocs"][str(b)], r["alloc_exponent"]))
    else:
        print(json.dumps(rows, indent=1))
    if args.write:
        with open(args.write, "w") as handle:
            json.dump({"tolerance": TOLERANCE, "sizes": sizes, "guards": rows}, handle, indent=1, sort_keys=True)
            handle.write("\n")
    if args.check:
        with open(args.check) as handle:
            guards = json.load(handle)["guards"]
        failures = []
        for name, guard in guards.items():
            got = rows.get(name)
            if not got:
                failures.append("%s: not measured" % name)
                continue
            for n, limit in guard["ir"].items():
                if n in got["ir"] and got["ir"][n] > limit * TOLERANCE + 50:
                    failures.append("%s n=%s: %.0f Ir/call > guard %.0f +10%%" % (name, n, got["ir"][n], limit))
            if got["exponent"] > guard["exponent"] + EXPONENT_SLACK:
                failures.append("%s: scaling exponent %.2f > %.2f" % (name, got["exponent"], guard["exponent"]))
        for failure in failures:
            print("GUARD FAIL " + failure)
        print("builtins_guards: %d workloads, %d guard failures" % (len(guards), len(failures)))
        return 1 if failures else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
