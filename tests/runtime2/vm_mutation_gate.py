#!/usr/bin/env python3
"""Runtime 2.0 CP12: live mutation gate for the differential test gates.

  vm_mutation_gate.py --nift .build/nift-mutants [--mutants 1,2,...] [--rt-diff]

Review round 2's acceptance criterion: the gates (exact legacy/VM comparison
with the vm_policies.py policies, the pinned golden results, the independent
integer reference) must reject every deliberate VM bug and pass the
unmutated binary. The binary is built with -DNIFT_VM_MUTANTS (make
.build/nift-mutants; normal builds contain no mutant code) and selects one
bug with NIFT_VM_MUTANT=n (src/rt/vm_mutants.h):

  1 Sub dispatched to Add in the checked table
  2 large slow-path integer results off by 1000
  3 spurious overflow in AddI for small operands (x >= 100)
  4 a dropped loop iteration (i += 1 from 1 skips 2)
  5 wrong error column (+1)
  6 wrong index number in the index message
  7 wrong kinds in the assignment-type message
  8 the Mod fast path's sign
  9 BindType ignores a text type held in a register (stage 4: a declaration
    from a parameter binds with the unknown type)

Each suite runs at a modest size: vm_int_bounds (every int opcode at the
bounds, Python ints), vm_golden (pinned D-018 results), vm_review_cp12,
vm_d018, the supported-subset generator (100 % VM, compared with legacy),
the general generator and VM corpus (vm_parity), the adversarial gate and
the former-replay suite; --rt-diff adds rt-diff over the shared corpora.
The unmutated run must pass every suite; each mutant must fail at least one
(all suites run for each mutant, so the report shows which reject it).
"""
import argparse
import os
import stat
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MUTANTS = {1: "Sub dispatched to Add in the checked table", 2: "large slow-path result off by 1000",
           3: "spurious overflow in AddI (small operands)", 4: "dropped loop iteration",
           5: "wrong error column", 6: "wrong index number in the index message",
           7: "wrong kinds in the assignment-type message", 8: "Mod fast path sign",
           9: "BindType ignores the text type register"}


def suites(nift, rt_diff):
    py = sys.executable
    t = lambda name: os.path.join(HERE, name)  # noqa: E731
    out = [
        ("int-bounds", [py, t("vm_int_bounds.py"), "--nift", nift]),
        ("golden", [py, t("vm_golden.py"), "--nift", nift]),
        ("review", [py, t("vm_review_cp12.py"), "--nift", nift]),
        ("d018", [py, t("vm_d018.py"), "--nift", nift]),
        ("supported", [py, t("vm_subset_gen.py"), "--nift", nift, "--suite", "supported", "--seed", "3",
                       "--count", "100"]),
        ("parity", [py, t("vm_parity.py"), "--nift", nift, "--gen", "100", "--seed", "3"]
         + (["--rt-diff", "--rt-diff-gen", "50"] if rt_diff else [])),
        ("adversarial", [py, t("vm_adversarial.py"), "--nift", nift, "--seed", "3", "--count", "40",
                         "--per-family", "120"]),
        ("former-replay", [py, t("vm_former_replay.py"), "--nift", nift, "--seed", "3", "--sample", "60"]),
        ("records", [py, t("vm_records_coverage.py"), "--nift", nift]),
    ]
    return out


def wrapper(binary, mutant, d):
    path = os.path.join(d, f"nift-mutant-{mutant}")
    with open(path, "w") as f:
        f.write(f"#!/bin/sh\nNIFT_VM_MUTANT={mutant}\nexport NIFT_VM_MUTANT\nexec '{binary}' \"$@\"\n")
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC)
    return path


def run_suites(nift, rt_diff, label, logs):
    rejected = []
    for name, cmd in suites(nift, rt_diff):
        start = time.time()
        p = subprocess.run(cmd, capture_output=True, text=True)
        tail = (p.stdout.strip().splitlines() or [""])[-1][:160]
        log = os.path.join(logs, f"{label}-{name}.log")
        with open(log, "w") as f:
            f.write(f"$ {' '.join(cmd)}\nexit {p.returncode}\n--- stdout\n{p.stdout}\n--- stderr\n{p.stderr}")
        print(f"  [{label}] {name}: {'PASS' if p.returncode == 0 else 'REJECT'} ({time.time() - start:.0f}s) {tail}",
              flush=True)
        if p.returncode != 0:
            rejected.append(name)
            if label == "0":  # the unmutated binary must pass: show why it did not
                print("\n".join("      | " + x for x in (p.stdout + p.stderr).strip().splitlines()[-25:]), flush=True)
    return rejected


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True, help="the -DNIFT_VM_MUTANTS build")
    ap.add_argument("--mutants", default=",".join(str(k) for k in MUTANTS))
    ap.add_argument("--rt-diff", action="store_true", help="also run rt-diff over the shared corpora")
    ap.add_argument("--logs", help="directory for every suite's full output (default: a temporary directory)")
    args = ap.parse_args()
    binary = os.path.abspath(args.nift)
    d = tempfile.mkdtemp(prefix="nift-vm-mutants-")
    logs = args.logs or os.path.join(d, "logs")
    os.makedirs(logs, exist_ok=True)
    print(f"full suite outputs: {logs}")
    failures = 0
    print("unmutated (NIFT_VM_MUTANT=0):", flush=True)
    rejected = run_suites(wrapper(binary, 0, d), args.rt_diff, "0", logs)
    if rejected:
        failures += 1
        print(f"FAIL unmutated binary rejected by {rejected}")
    results = {}
    for m in (int(x) for x in args.mutants.split(",") if x):
        print(f"mutant {m} ({MUTANTS[m]}):", flush=True)
        results[m] = run_suites(wrapper(binary, m, d), args.rt_diff, str(m), logs)
        if not results[m]:
            failures += 1
            print(f"FAIL mutant {m} ({MUTANTS[m]}) survived every suite")
    for m, by in results.items():
        print(f"mutant {m} {MUTANTS[m]}: {'REJECTED by ' + ', '.join(by) if by else 'SURVIVED'}")
    print(("PASS" if not failures else "FAIL") + f" vm mutation gate: unmutated passes, {len(results)} mutants, "
          f"{sum(1 for v in results.values() if v)} rejected, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
