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
 10 (compiler) a member or index path argument of a user call is admitted
    whatever its kinds, aggregates and uncertain kinds included (stage 4,
    path-argument ruling)
 11 a missing member in an admitted path argument gets the value wording
    (`value has no member: k`), not legacy's location wording
 12 (prepared analysis) a parameter copy `out := p` is admitted although a
    binding p outside the function is double typed (stage 4, outer-binding
    ruling)

Each suite runs at a modest size: vm_int_bounds (every int opcode at the
bounds, Python ints), vm_golden (pinned D-018 results), vm_review_cp12,
vm_d018, the supported-subset generator (100 % VM, compared with legacy),
the general generator and VM corpus (vm_parity), the adversarial gate and
the former-replay suite; --rt-diff adds rt-diff over the shared corpora.
The unmutated run must pass every suite; each mutant must fail at least one
(all suites run for each mutant, so the report shows which reject it).
"""
import argparse
import concurrent.futures as cf
import os
import stat
import subprocess
import sys
import tempfile
import threading
import time

# Suites that enforce wall-clock limits per program; never run two at once.
TIMING_SENSITIVE = {"adversarial", "former-replay"}

HERE = os.path.dirname(os.path.abspath(__file__))
MUTANTS = {1: "Sub dispatched to Add in the checked table", 2: "large slow-path result off by 1000",
           3: "spurious overflow in AddI (small operands)", 4: "dropped loop iteration",
           5: "wrong error column", 6: "wrong index number in the index message",
           7: "wrong kinds in the assignment-type message", 8: "Mod fast path sign",
           9: "BindType ignores the text type register", 10: "path argument admitted whatever its kinds",
           11: "value wording for a missing member in a path argument",
           12: "double-typed outer binding of a copied parameter admitted"}


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


def run_one(label, name, cmd, logs):
    start = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True)
    tail = (p.stdout.strip().splitlines() or [""])[-1][:160]
    with open(os.path.join(logs, f"{label}-{name}.log"), "w") as f:
        f.write(f"$ {' '.join(cmd)}\nexit {p.returncode}\n--- stdout\n{p.stdout}\n--- stderr\n{p.stderr}")
    # A suite counts only if it ran to its own summary: rc 0 with a final "PASS ..."
    # line, or rc != 0 with a final "FAIL ..." line. Anything else (crash, abort,
    # missing summary) is ABORTED, never a rejection: a mutant is not "rejected"
    # by a suite that did not finish.
    if p.returncode == 0 and tail.startswith("PASS "):
        verdict = "PASS"
    elif p.returncode != 0 and tail.startswith("FAIL "):
        verdict = "REJECT"
    else:
        verdict = "ABORTED"
    lines = [f"  [{label}] {name}: {verdict} (rc {p.returncode}, {time.time() - start:.0f}s) {tail}"]
    if verdict != "PASS" and (label == "0" or verdict == "ABORTED"):
        lines += ["      | " + x for x in (p.stdout + p.stderr).strip().splitlines()[-25:]]
    return verdict, lines


def run_all(labels, rt_diff, logs, jobs):
    """Run every (binary, suite) pair; pairs are independent (each suite works in
    its own temporary directory), so they may run concurrently. Results and the
    printed report are identical to the sequential order whatever --jobs is."""
    work = [(label, name, cmd) for label, nift in labels for name, cmd in suites(nift, rt_diff)]
    # Suites with their own wall-clock budgets (deep recursion, large-output
    # bail-outs, per-program timeouts) must not run concurrently with each
    # other: under oversubscription their timeouts flip outcomes (measured
    # 2026-10-20: --jobs 8 on 4 vCPU made the unmutated adversarial suite time
    # out). At most one of them runs at a time; the rest are scheduled freely.
    serial = threading.Semaphore(1)

    def one(w):
        if w[1] in TIMING_SENSITIVE:
            with serial:
                return run_one(w[0], w[1], w[2], logs)
        return run_one(w[0], w[1], w[2], logs)

    with cf.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        done = list(pool.map(one, work))
    rejected = {label: [] for label, _ in labels}
    aborted = []
    current = None
    if len(done) != len(work):
        aborted.append(("*", f"{len(work) - len(done)} planned check(s) produced no result"))
    for (label, name, _), (verdict, lines) in zip(work, done):
        if label != current:
            current = label
            print("unmutated (NIFT_VM_MUTANT=0):" if label == "0" else f"mutant {label} ({MUTANTS[int(label)]}):")
        print("\n".join(lines), flush=True)
        if verdict == "REJECT":
            rejected[label].append(name)
        elif verdict == "ABORTED":
            aborted.append((label, name))
    return rejected, aborted


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True, help="the -DNIFT_VM_MUTANTS build")
    ap.add_argument("--mutants", default=",".join(str(k) for k in MUTANTS))
    ap.add_argument("--rt-diff", action="store_true", help="also run rt-diff over the shared corpora")
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("NIFT_MUTANT_JOBS", "1")),
                    help="(binary, suite) pairs run concurrently (default 1: sequential, NIFT_MUTANT_JOBS)")
    ap.add_argument("--logs", help="directory for every suite's full output (default: a temporary directory)")
    args = ap.parse_args()
    binary = os.path.abspath(args.nift)
    d = tempfile.mkdtemp(prefix="nift-vm-mutants-")
    logs = args.logs or os.path.join(d, "logs")
    os.makedirs(logs, exist_ok=True)
    print(f"full suite outputs: {logs}")
    failures = 0
    mutants = [int(x) for x in args.mutants.split(",") if x]
    labels = [("0", wrapper(binary, 0, d))] + [(str(m), wrapper(binary, m, d)) for m in mutants]
    rej, aborted = run_all(labels, args.rt_diff, logs, args.jobs)
    for label, name in aborted:
        failures += 1
        print(f"FAIL check [{label}] {name} did not complete (no summary line or inconsistent exit status)")
    rejected = rej["0"]
    if rejected:
        failures += 1
        print(f"FAIL unmutated binary rejected by {rejected}")
    results = {m: rej[str(m)] for m in mutants}
    for m in mutants:
        if not results[m]:
            failures += 1
            print(f"FAIL mutant {m} ({MUTANTS[m]}) survived every suite")
    for m, by in results.items():
        print(f"mutant {m} {MUTANTS[m]}: {'REJECTED by ' + ', '.join(by) if by else 'SURVIVED'}")
    print(f"matrix {len(labels)} binaries x {len(suites(binary, args.rt_diff))} suites, {len(aborted)} incomplete check(s)")
    print(("PASS" if not failures else "FAIL") + f" vm mutation gate: unmutated passes, {len(results)} mutants, "
          f"{sum(1 for v in results.values() if v)} rejected, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
