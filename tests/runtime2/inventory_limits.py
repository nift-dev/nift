#!/usr/bin/env python3
"""classify-all compiles with a real script run's settings (slot 1, owner 2026-11-11 05:41, decisions.md D-068 #11).

src/rt/refusal_inventory.cpp calls cooperative_switches(limits), as src/rt/script_runner.cpp does, so
NIFT_VM=classify-all no longer lists refusals that only the default CompileLimits produced (the F12 rows of
tests/runtime2/cp25/classification.tsv, retired in slot 1). The probes below run on the VM; with the
default limits classify-all reported F12 reasons for every one of them.

  real build:  classify-all reports no F12 reason for any probe, and the probes run on the VM.
  --mutant:    the build with NIFT_INVENTORY_LIMITS_MUTANT (the call reverted). classify-all reports the
               F12 reasons again, and the classification gate (inventory.py report --check against the
               committed tsv, on a collect directory of these probes) fails with "retired reason(s) seen
               again" naming them. The suite passes only if both happen (a caught mutant).

usage: inventory_limits.py --nift NIFT [--mutant]
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
INVENTORY = os.path.join(HERE, "cp25", "inventory.py")
CORPUS = os.path.join(HERE, "rt-diff-corpus", "async")

PROBES = {
    "fileops": open(os.path.join(CORPUS, "fileops", "main.f")).read(),
    "thread": open(os.path.join(CORPUS, "thread", "main.f")).read(),
    "async-observe": 'fn[async](af(x)) { print("in")\nreturn x }\nfu := af(1)\nprint(fu.done())\n'
                     'print(fu.status())\nprint(await fu)\n',
    "struct-in-thread": "struct(P) { v := 1 }\nfn(w()) { p := P()\nreturn p.v }\nt := thread(w)\nprint(t.join())\n",
    "collection-callback": "fn[async](a(x)) { return x }\nxs := [2, 1].map((v) => v + 1)\nprint(await a(xs.size()))\n",
}
# The F12 reasons each probe reported with the default limits (measured on the mutant build).
EXPECTED_MUTANT = {
    "fileops": {"coro-blocking-host:exists", "coro-blocking-host:file", "coro-blocking-host:rm", "method:close",
                "method:open", "method:save", "method:write"},
    "thread": {"callable:thread", "method:join"},
    "async-observe": {"coro-observe:done", "coro-observe:print", "coro-observe:status", "method:done",
                      "method:status"},
    "struct-in-thread": {"callable:thread", "method:join"},
    "collection-callback": {"coro-collection-callback"},
}
F12 = set().union(*EXPECTED_MUTANT.values())
# method:join stays E2+D, PENDING-OWNER (its 9 CP24/lambda-variadic sightings remain with the fix), so it is
# not a retired row; every other F12 reason above is retired in slot 1 and must be reported as seen again.
RETIRED = F12 - {"method:join"}


def classify(nift, d, name, src):
    work = os.path.join(d, name)
    os.makedirs(work)
    with open(os.path.join(work, "main.f"), "w") as f:
        f.write(src)
    stats = os.path.join(work, "stats.jsonl")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": "/nonexistent", "NIFT_VM": "classify-all",
           "NIFT_VM_STATS": stats, "LANG": "C"}
    p = subprocess.run([nift, "main.f"], cwd=work, env=env, capture_output=True, timeout=120)
    if p.returncode != 0:  # classify-all compiles only; a non-zero exit is a harness failure
        return None, set()
    records = [json.loads(l) for l in open(stats)] if os.path.exists(stats) else []
    reasons = {r["reason"].split(" ")[0] for j in records for r in j.get("reasons") or [] if not r.get("consequential")}
    return records, reasons


def runs_on_vm(nift, d, name):
    work = os.path.join(d, name)
    stats = os.path.join(work, "run-stats.jsonl")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": work, "NIFT_VM_STATS": stats, "LANG": "C"}
    p = subprocess.run([nift, "main.f"], cwd=work, env=env, capture_output=True, timeout=120)
    outcomes = [json.loads(l).get("outcome") for l in open(stats)] if os.path.exists(stats) else []
    return p.returncode == 0 and outcomes and all(o == "vm" for o in outcomes)


def gate(d, units):
    coll = os.path.join(d, "collect")
    os.makedirs(coll)
    with open(os.path.join(coll, "classify-all.jsonl"), "w") as f:
        for name, records in units.items():
            for j in records:
                j = dict(j, id="probe/%s|main.f" % name)
                f.write(json.dumps(j, sort_keys=True) + "\n")
    open(os.path.join(coll, "execute.jsonl"), "w").close()
    p = subprocess.run([sys.executable, "-B", INVENTORY, "report", coll, "--check", "--markdown",
                        os.path.join(d, "report.md")], capture_output=True, text=True, timeout=120)
    return p.returncode, p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--mutant", action="store_true")
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    failures = []
    with tempfile.TemporaryDirectory(prefix="inventory-limits-") as d:
        units = {}
        for name, src in PROBES.items():
            records, reasons = classify(nift, d, name, src)
            units[name] = records
            f12 = reasons & F12
            if not records:
                failures.append("%s: harness: classify-all failed or wrote no stats line" % name)
                records = []
            elif a.mutant and f12 != EXPECTED_MUTANT[name]:
                failures.append("%s: mutant classify-all F12 reasons %s, expected %s" %
                                (name, sorted(f12), sorted(EXPECTED_MUTANT[name])))
            elif not a.mutant and f12:
                failures.append("%s: classify-all still reports F12 reasons %s" % (name, sorted(f12)))
            if not a.mutant and not runs_on_vm(nift, d, name):
                failures.append("%s: the probe does not run on the VM" % name)
        rc, err = gate(d, units)
        retired_line = [l for l in err.split("\n") if "retired reason(s) seen again" in l]
        if a.mutant:
            named = set(retired_line[0].split("seen again: ", 1)[1].split(", ")) if retired_line else set()
            if rc != 3 or named != RETIRED:
                failures.append("classification gate on the mutant: exit %d, retired seen again %s (expected all of %s)"
                                % (rc, sorted(named), sorted(RETIRED)))
        elif retired_line or "G1 PASS" not in err:
            failures.append("classification gate on the real build: exit %d %r" % (rc, err[-400:]))
    label = "inventory limits mutant" if a.mutant else "inventory limits"
    for f in failures:
        print("FAIL %s: %s" % (label, f))
    if a.mutant:
        print("%s %s: %s" % ("FAIL" if failures else "PASS", label,
                             "not caught" if failures else
                             "the reverted limits list the %d F12 reasons again and the classification gate fails "
                             "(the %d retired ones seen again)" % (len(F12), len(RETIRED))))
    else:
        print("%s %s: %d probes, no F12 reason in classify-all, all run on the VM" %
              ("FAIL" if failures else "PASS", label, len(PROBES)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
