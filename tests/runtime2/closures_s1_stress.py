#!/usr/bin/env python3
"""CP12C Slice 1 stress fixtures: the 1,000,000 self-referencing closure loop
under an 8 MB VM heap budget, and one escaping cycle while the rest burn.

  closures_s1_stress.py --nift ./nift --prepare   # Phase A: still refused
  closures_s1_stress.py --nift ./nift             # Phase B: must run on the VM

In Phase B the VM statistics must also show collection activity and bounded
live heap (fields added with the closure collector). A timeout, budget error,
legacy execution or memory-budget stop is a failure.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "closures-s1", "stress")
FILES = [("million_acyclic.f", b"1\n", 0), ("escape_while_burning.f", b"0\n1\n", None)]
# A1: these must be refused by accounted heap, not silently succeed at large RSS.
MEMORY_REFUSALS = ["h_wide.f", "h_nocap_200k.f"]
# A1 lower bound: 1000 retained capture-free closures must be charged.
LIVE_BOUND_FILE = "h_live_simple.f"
LIVE_BOUND_COUNT = 1000
LIVE_BOUND_MIN = LIVE_BOUND_COUNT * 128


def run(nift, work, name, mode, budget):
    d = tempfile.mkdtemp(dir=work)
    shutil.copy(os.path.join(FIXTURES, name), os.path.join(d, name))
    stats = os.path.join(d, "stats.jsonl")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d,
           "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": mode,
           "NIFT_VM_STATS": stats, "NIFT_VM_HEAP_BUDGET": str(budget)}
    p = subprocess.run(["timeout", "180", nift, name], cwd=d, env=env, stdin=subprocess.DEVNULL,
                       capture_output=True)
    st = None
    if os.path.exists(stats):
        try:
            st = json.loads(open(stats).read().splitlines()[-1])
        except (OSError, IndexError, ValueError):
            st = None
    return (p.stdout, p.stderr, p.returncode), st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--budget", type=int, default=8_000_000)
    ap.add_argument("--prepare", action="store_true")
    ap.add_argument("--mutant", action="store_true")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = 0
    detected = set()
    with tempfile.TemporaryDirectory(prefix="closures-s1-stress-") as work:
        for name, want_out, want_live in FILES:
            if args.prepare:
                (out, err, code), st = run(nift, work, name, "classify", args.budget)
                print(f"ok   prepare {name}: {st and (st.get('outcome'), st.get('reason'))}")
                continue
            (out, err, code), st = run(nift, work, name, "1", args.budget)
            problems = []
            if st is None:
                problems.append("no statistics")
            else:
                if st.get("outcome") != "vm" or st.get("vm_executions") != 1:
                    problems.append(f"outcome {st.get('outcome')}/{st.get('reason')}")
                peak = st.get("heap_peak")
                if peak is None or peak > args.budget:
                    problems.append(f"live-heap peak {peak} > budget {args.budget}")
                if want_live is not None and st.get("heap_live", 0) != want_live:
                    problems.append(f"live heap at stop {st.get('heap_live')} != {want_live}")
                if st.get("beyond_legacy_depth"):
                    problems.append("unexpected recursion-depth stop")
            if out != want_out or code != 0:
                problems.append(f"output {out!r} exit {code} stderr {err[-160:]!r}")
            if args.mutant:
                if code < 0 or code >= 128:
                    failures += 1
                    print(f"FAIL mutant {name}: harness: VM process crashed (exit {code})")
                elif any("no statistics" in p for p in problems):
                    failures += 1
                    print(f"FAIL mutant {name}: harness: " + "; ".join(problems))
                elif problems:
                    detected.add(name)
                    print(f"ok   mutant {name}: rejected ({'; '.join(problems)})")
                continue
            if problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name}: live_peak={st.get('heap_peak')} live_at_stop={st.get('heap_live')}")
        # A1 memory refusals and the charged-live lower bound.
        for name in MEMORY_REFUSALS:
            (out, err, code), st = run(nift, work, name, "require", 8_000_000)
            problems = []
            if code == 0 or (st and st.get("outcome") == "vm"):
                problems.append("accounted budget did not refuse the retained closures")
            if st is None:
                problems.append("no statistics")
            elif st.get("heap_peak", 0) < 7_000_000:
                problems.append(f"heap_peak {st.get('heap_peak')} did not charge the closures")
            if b"memory budget" not in err.lower():
                problems.append(f"not a clean memory-budget refusal: {err[-160:]!r}")
            if args.mutant:
                if code < 0 or code >= 128:
                    failures += 1
                    print(f"FAIL mutant {name}: harness: VM process crashed (exit {code})")
                elif any("no statistics" in p for p in problems):
                    failures += 1
                    print(f"FAIL mutant {name}: harness: " + "; ".join(problems))
                elif problems:
                    detected.add(name)
                    print(f"ok   mutant {name}: rejected ({'; '.join(problems)})")
            elif problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name}: charged refusal (peak {st.get('heap_peak')})")
        (out, err, code), st = run(nift, work, LIVE_BOUND_FILE, "require", 64_000_000)
        problems = []
        if code != 0 or out != b"1000\n" or not st or st.get("outcome") != "vm":
            problems.append(f"live-bound program failed: output {out!r} code {code}")
        elif st.get("heap_peak", 0) < LIVE_BOUND_MIN:
            problems.append(f"heap_peak {st.get('heap_peak')} < {LIVE_BOUND_MIN}")
        if args.mutant:
            if any("no statistics" in p for p in problems):
                failures += 1
                print(f"FAIL mutant {LIVE_BOUND_FILE}: harness: " + "; ".join(problems))
            elif problems:
                detected.add(LIVE_BOUND_FILE)
                print(f"ok   mutant {LIVE_BOUND_FILE}: rejected ({'; '.join(problems)})")
        elif problems:
            failures += 1
            print(f"FAIL {LIVE_BOUND_FILE}: " + "; ".join(problems))
        else:
            print(f"ok   {LIVE_BOUND_FILE}: peak {st.get('heap_peak')} >= {LIVE_BOUND_MIN}")
    if args.mutant:
        if not detected:
            print("FAIL closures-s1 mutant stress: no detection")
            return 1
        print(f"{'PASS' if not failures else 'FAIL'} closures-s1 mutant stress: detected {len(detected)} cases, {failures} harness failure(s)")
        return 1 if failures else 0
    print(f"{'PASS' if not failures else 'FAIL'} closures-s1 stress: {len(FILES) + len(MEMORY_REFUSALS) + 1} cases, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
