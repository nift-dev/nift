#!/usr/bin/env python3
"""Guard for `make bench-rt-value`: rt instruction counts (callgrind) per
workload must not exceed the locked baseline by more than the tolerance.

  value_bench_guard.py BENCH_TABLE BASELINE_JSON
  value_bench_guard.py --self-test BASELINE_JSON

The self-test proves the guard on synthetic tables built from the baseline:
each workload at +1.9 % passes, any single workload (object set included) at
+2.1 % fails, and a missing workload fails.

BENCH_TABLE is tools/rt-bench.sh's markdown output. The baseline records the
toolchain it was taken with; counts from another compiler are reported, not
judged (instruction counts are deterministic only for one toolchain).
"""
import json
import re
import subprocess
import sys


def judge(table, baseline, toolchain):
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
    for name, why in baseline.get("approved_exceptions", {}).items():
        print("approved exception (locked in the baseline): %s: %s" % (name, why))
    print("PASS value bench guard" if not bad else "FAIL value bench guard")
    return 1 if bad else 0


def table_of(counts):
    return "".join("| %s | %d | 1 | 0.0 |\n" % (k, v) for k, v in counts.items())


def self_test(baseline):
    tc = baseline["toolchain"]
    locked = baseline["rt_ir"]
    failures = 0
    def expect(label, counts, want_fail):
        nonlocal failures
        got = judge(table_of(counts), baseline, tc)
        if bool(got) != want_fail:
            failures += 1
            print("SELF-TEST FAIL: %s" % label)
    expect("all at +1.9 %", {k: int(v * 1.019) for k, v in locked.items()}, False)
    for name in locked:
        counts = dict(locked)
        counts[name] = int(locked[name] * 1.021) + 1
        expect("%s at +2.1 %%" % name, counts, True)
    missing = dict(locked)
    missing.pop(next(iter(missing)))
    expect("a missing workload", missing, True)
    print("PASS value bench guard self-test" if not failures else "FAIL value bench guard self-test")
    return 1 if failures else 0


def main():
    if sys.argv[1] == "--self-test":
        return self_test(json.load(open(sys.argv[2])))
    table, baseline = open(sys.argv[1]).read(), json.load(open(sys.argv[2]))
    toolchain = subprocess.run(["c++", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    return judge(table, baseline, toolchain)


if __name__ == "__main__":
    sys.exit(main())
