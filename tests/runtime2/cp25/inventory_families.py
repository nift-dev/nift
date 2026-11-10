#!/usr/bin/env python3
"""inventory.py family rows and exact rows (slot 1, decisions.md D-068 #11; family grouping D-067 #10).

Only the four approved families (builtin-arity, builtin-receiver, method, callable) have a family row
`<prefix>:*` that classifies an unlisted member. Every other reason needs its own exact row: an unlisted
`type:<op>` reason fails G1. Since slot 1 the multiplication refusal is `type:mul`, and `type:*` is
no longer a row at all.

The cases run `inventory.py report --check` on small synthetic collect directories against a temporary
classification, so the real tsv is not involved:

  family-member          `method:zzz` with only `method:*` listed: G1 passes (the family row classifies it)
  unlisted-type-reason   `type:zzz` unlisted: G1 fails with "unclassified reason: type:zzz"
  old-type-star-spelling `type:*` seen, with a `type:*` row absent: G1 fails (the old spelling is not a row)
  type-mul-exact         `type:mul` with its exact row: G1 passes
  real-tsv-type-rows     the committed tsv has no `type:*` row and has a `type:mul` row

--mutant re-adds the generic family fallback (any `<prefix>:*`) to a scratch copy of inventory.py and
runs the same cases against it: the negative control. The suite must then fail, on the named case
unlisted-type-reason (a `type:*` row in the temporary classification would classify `type:zzz`).

usage: inventory_families.py [--mutant]
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
INVENTORY = os.path.join(HERE, "inventory.py")
TSV = os.path.join(HERE, "classification.tsv")
HEADER = "reason\tclass\tfamily\tlegacy\tdisposition\tapproval\tevidence\traised_at\tstatus\n"

EXACT = '''        prefix = reason.split(":")[0]
        return cls.get(prefix + ":*") if ":" in reason and prefix in FAMILIES else None
'''
GENERIC = '''        return cls.get(reason.split(":")[0] + ":*") if ":" in reason else None
'''


def row(reason, status="PENDING-OWNER"):
    return "\t".join([reason, "A", "F-test", "legacy", "disposition", "approval", "evidence", "", status]) + "\n"


def report(inventory, work, seen, rows):
    d = os.path.join(work, "collect")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "classify-all.jsonl"), "w") as f:
        f.write(json.dumps({"id": "unit|main.f", "reasons": [{"reason": r} for r in seen]}) + "\n")
    open(os.path.join(d, "execute.jsonl"), "w").close()
    tsv = os.path.join(work, "classification.tsv")
    with open(tsv, "w") as f:
        f.write(HEADER + "".join(row(r) for r in rows))
    p = subprocess.run([sys.executable, "-B", inventory, "report", d, "--classification", tsv, "--check",
                        "--markdown", os.path.join(work, "out.md")], capture_output=True, text=True, timeout=120)
    return p.returncode, p.stderr


def cases(inventory):
    failures = []
    with tempfile.TemporaryDirectory(prefix="inv-families-") as work:
        rc, err = report(inventory, os.path.join(work, "a"), ["method:zzz"], ["method:*", "type:*"])
        if "G1 PASS" not in err:
            failures.append(("family-member", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "b"), ["type:zzz"], ["method:*", "type:*", "type:mul"])
        if rc != 1 or "unclassified reason: type:zzz" not in err:
            failures.append(("unlisted-type-reason", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "c"), ["type:*"], ["method:*", "type:mul"])
        if rc != 1 or "unclassified reason: type:*" not in err:
            failures.append(("old-type-star-spelling", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "d"), ["type:mul"], ["type:mul"])
        if "G1 PASS" not in err:
            failures.append(("type-mul-exact", "rc %d %r" % (rc, err[-300:])))
    reasons = [l.split("\t", 1)[0] for l in open(TSV) if not l.startswith("#")]
    if "type:*" in reasons or "type:mul" not in reasons:
        failures.append(("real-tsv-type-rows", "type:* present %s, type:mul present %s" %
                         ("type:*" in reasons, "type:mul" in reasons)))
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mutant", action="store_true")
    a = ap.parse_args()
    if not a.mutant:
        failures = cases(INVENTORY)
        for name, why in failures:
            print("FAIL inventory families: %s: %s" % (name, why))
        print("%s inventory families: 5 cases (family rows only for the four D-067 #10 families)" %
              ("FAIL" if failures else "PASS"))
        return 1 if failures else 0
    text = open(INVENTORY).read()
    if text.count(EXACT) != 1:
        print("FAIL inventory families mutant: harness: the exact-family code is not in inventory.py (stale mutant)")
        return 1
    with tempfile.TemporaryDirectory(prefix="inv-families-mutant-") as work:
        # Same depth below a root as tests/runtime2/cp25/ (inventory.py computes REPO = parents[2]).
        copy = os.path.join(work, "tests", "runtime2", "cp25", "inventory.py")
        os.makedirs(os.path.dirname(copy))
        with open(copy, "w") as f:
            f.write(text.replace(EXACT, GENERIC))
        failures = cases(copy)
    if any("Traceback" in why for _, why in failures):
        print("FAIL inventory families mutant: harness: the mutant copy crashed (not a detection): %r" % failures)
        return 1
    names = sorted(n for n, _ in failures)
    if names != ["unlisted-type-reason"]:
        print("FAIL inventory families mutant: expected only unlisted-type-reason to fail, got %s" % (names or "none"))
        return 1
    print("PASS inventory families mutant: a generic family fallback is caught by unlisted-type-reason")
    return 0


if __name__ == "__main__":
    sys.exit(main())
