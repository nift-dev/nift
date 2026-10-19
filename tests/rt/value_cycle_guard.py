#!/usr/bin/env python3
"""Guard for the D-040 cycle-check cost (V16), isolated from the store itself.

  value_cycle_guard.py CHECKED_BENCH UNCHECKED_BENCH [iterations]

Runs both builds of tests/rt/value_cycle_bench.cpp alternately (5 rounds,
best of each), prints ns per store with and without the check, and fails when
the difference per store exceeds its budget. The guard is on the difference,
not on the ratio to plain stores: closure and handle stores have their own
costs. Plain stores must not pay for the check at all (the O(1) summary test).
"""
import subprocess
import sys

BUDGET_NS = {  # added cost per store allowed for the check
    "store_plain": 2.0,
    "store_callable": 50.0,
    "store_handle": 40.0,
    "store_nested_identity": 200.0,
}


def run(binary, iterations):
    out = subprocess.run([binary, str(iterations)], capture_output=True, text=True, timeout=600)
    if out.returncode != 0:
        raise SystemExit("%s failed: %s" % (binary, out.stderr))
    return {line.split()[0]: float(line.split()[1]) for line in out.stdout.splitlines() if line.strip()}


def main():
    checked, unchecked = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 2000000
    best = {"on": {}, "off": {}}
    for _ in range(5):
        for tag, binary in (("on", checked), ("off", unchecked)):
            for name, ns in run(binary, iterations).items():
                best[tag][name] = min(ns, best[tag].get(name, ns))
    bad = 0
    for name, budget in BUDGET_NS.items():
        on, off = best["on"][name], best["off"][name]
        diff = on - off
        verdict = "ok" if diff <= budget else "FAIL"
        bad += verdict == "FAIL"
        print("%-24s with check %8.2f ns  without %8.2f ns  difference %8.2f ns (budget %.0f) %s"
              % (name, on, off, diff, budget, verdict))
    print("PASS cycle-check cost guard" if not bad else "FAIL cycle-check cost guard")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
