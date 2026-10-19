#!/usr/bin/env python3
"""Guard for `make bench-rt-value`: rt instruction counts (callgrind) per
workload must not exceed the locked baseline by more than the tolerance.

  value_bench_guard.py BENCH_TABLE BASELINE_JSON

BENCH_TABLE is tools/rt-bench.sh's markdown output. The baseline records the
toolchain it was taken with; counts from another compiler are reported, not
judged (instruction counts are deterministic only for one toolchain).
"""
import json
import re
import subprocess
import sys


def main():
    table, baseline_path = open(sys.argv[1]).read(), sys.argv[2]
    baseline = json.load(open(baseline_path))
    toolchain = subprocess.run(["c++", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    rows = {m.group(1): int(m.group(2)) for m in re.finditer(r"^\| (.+?) \| (\d+) \| \d+ \|", table, re.M)}
    judged = toolchain == baseline["toolchain"]
    bad = 0
    for name, locked in baseline["rt_ir"].items():
        got = rows.get(name)
        if got is None:
            print("FAIL missing workload: %s" % name)
            bad += 1
            continue
        limit = locked * (1 + baseline["tolerance"])
        verdict = "ok" if got <= limit else ("FAIL" if judged else "over (not judged)")
        bad += verdict == "FAIL"
        print("%-45s locked %12d  now %12d  %+6.2f%%  %s" % (name, locked, got, 100.0 * (got - locked) / locked, verdict))
    if not judged:
        print("toolchain differs from the baseline's (%s vs %s): counts reported, not judged" % (toolchain, baseline["toolchain"]))
    print("PASS value bench guard" if not bad else "FAIL value bench guard")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
