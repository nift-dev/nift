#!/usr/bin/env python3
"""CP17 typed handles: instruction guards on create/resolve cost (callgrind).

Runs tests/rt/handle_table_bench.cpp under callgrind for each mode at two
iteration counts and reports the marginal instructions per iteration,
(Ir(n2) - Ir(n1)) / (n2 - n1), which excludes start-up and table setup.

  handle_guards.py <handle-bench> [--write GUARDS.json] [--check GUARDS.json]

--write records the measurement; --check fails when a mode exceeds its
recorded value by more than 10 % (the CP14 convention). Run under heavy.sh.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

TOLERANCE = 1.10
MODES = ["cycle", "resolve", "counted", "context"]
N1, N2 = 2000, 22000


def total_ir(bench, mode, n, workdir):
    out = os.path.join(workdir, "cg.out")
    subprocess.run(["valgrind", "--tool=callgrind", "--callgrind-out-file=" + out, bench, mode, str(n)],
                   capture_output=True, text=True, check=True, timeout=600)
    with open(out) as handle:
        for line in handle:
            m = re.match(r"(?:summary|totals): (\d+)", line)
            if m:
                return int(m.group(1))
    raise RuntimeError("no callgrind total for %s %d" % (mode, n))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("bench")
    parser.add_argument("--write")
    parser.add_argument("--check")
    args = parser.parse_args()
    results = {}
    with tempfile.TemporaryDirectory() as workdir:
        for mode in MODES:
            a = total_ir(args.bench, mode, N1, workdir)
            b = total_ir(args.bench, mode, N2, workdir)
            results[mode] = round((b - a) / (N2 - N1), 1)
            print("%-8s %8.1f Ir/iteration" % (mode, results[mode]))
    if args.write:
        with open(args.write, "w") as handle:
            json.dump({"tolerance": TOLERANCE, "n": [N1, N2], "ir_per_iteration": results}, handle, indent=2)
            handle.write("\n")
    failed = False
    if args.check:
        with open(args.check) as handle:
            guards = json.load(handle)["ir_per_iteration"]
        for mode, value in results.items():
            limit = guards[mode] * TOLERANCE
            status = "PASS" if value <= limit else "FAIL"
            failed = failed or status == "FAIL"
            print("%s %-8s %.1f <= %.1f (guard %.1f +10%%)" % (status, mode, value, limit, guards[mode]))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
