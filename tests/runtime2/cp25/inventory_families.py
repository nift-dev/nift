#!/usr/bin/env python3
"""inventory.py family rows and exact rows (slot 1, decisions.md D-068 #11; family grouping D-067 #10).

Only the four approved families (builtin-arity, builtin-receiver, method, callable) have a family row
`<prefix>:*` that classifies an unlisted member. Every other reason needs its own exact row: an unlisted
`type:<op>` reason fails G1. Since slot 1 the multiplication refusal is `type:mul`, and `type:*` is
no longer a row at all.

The cases run `inventory.py report --check` on small synthetic collect directories against a temporary
classification, so the real tsv is not involved:

  family-member          `method:zzz` with only `method:*` listed: G1 passes (the family row classifies it)
  family-member-blocks   `callable:zzz` and `method:zzz`, classified only through their PENDING-OWNER family rows:
                         --check blocks (exit 3) and names both (review of C125, R1)
  real-tsv-family-rows   the committed tsv's four family rows (builtin-arity:*, builtin-receiver:*, method:*,
                         callable:*) are PENDING-OWNER: a family row never approves unlisted members
  unlisted-type-reason   `type:zzz` unlisted: G1 fails with "unclassified reason: type:zzz"
  old-type-star-spelling `type:*` seen, with a `type:*` row absent: G1 fails (the old spelling is not a row)
  type-mul-exact         `type:mul` with its exact row: G1 passes
  real-tsv-type-rows     the committed tsv has no `type:*` row and has a `type:mul` row

The status cases (owner 2026-11-11 11:07, decisions.md D-069 #4: the five implementation statuses):

  status-accepted-seen     an `implemented-accepted` reason seen again: --check blocks (exit 3)
  status-awaiting-unseen   an `implemented-awaiting-review` row, not seen: --check still blocks (exit 3)
  status-accepted-unseen   an `implemented-accepted` row, not seen, the seen reason approved: exit 0
  status-invalid-unseen    an unseen row with an unknown status (`implemented`): G1 fails (exit 1)

The deferred-site cases (REVIEW-C136-SLOT3 F-A: a slot-3 `deferred:<reason>` record is not a refusal):

  deferred-accepted-not-seen     an implemented-accepted row seen only as a deferred site: exit 0
  deferred-retired-not-seen      a retired row seen only as a deferred site: exit 0
  accepted-refusal-still-blocks  the same row seen as a deferred site and as a refusal: --check blocks (exit 3)
  deferred-needs-row             a deferred site with no row: G1 fails (exit 1)
  deferred-pending-blocks        a deferred site of a PENDING-OWNER row: --check blocks (exit 3)

The D-077 #1 cases (owner 2026-11-12 04:25: DEFERRED — POST-CERTIFICATION IMPLEMENTATION, status
`deferred-post-cert`, each with a post_cert_deferrals.tsv entry and a unit scope):

  postcert-complete           a deferred-post-cert reason seen in its scoped unit with its recorded root: exit 0, the
                              report says "Inventory complete, with named deferred capabilities" and lists it as a
                              named deferred capability, never "all supported functionality passes"
  postcert-outside-scope      the same reason seen in a unit outside the scope: --check blocks (exit 3)
  postcert-root-drift         the scoped unit's real run refuses with another reason than the recorded root: blocks (exit 3)
  postcert-missing-entry      a deferred-post-cert row with no deferral entry: G1 fails (exit 1)
  postcert-entry-without-row  a deferral entry whose row is not deferred-post-cert: G1 fails (exit 1)
  postcert-pending-blocks     a deferred-post-cert reason in scope next to a PENDING-OWNER reason: exit 3, and the
                              completeness wording is not printed
  real-tsv-post-cert          the committed tsv and post_cert_deferrals.tsv agree (every deferred-post-cert row has an
                              entry citing D-077 #1), and the refusal gates (approved_refusal.py/.sh) do not accept
                              deferred-post-cert: a refusal-contract pass of a deferred capability is not a pass

--mutant family re-adds the generic family fallback (any `<prefix>:*`) to a scratch copy of inventory.py
and runs the same cases against it: the negative control. The suite must then fail, on the named case
unlisted-type-reason (a `type:*` row in the temporary classification would classify `type:zzz`).
--mutant status drops the two implemented-status checks from the copy: the suite must then fail on
exactly status-accepted-seen, status-awaiting-unseen and accepted-refusal-still-blocks. --mutant validity drops the check that every
unseen row has a valid status: the suite must then fail on exactly status-invalid-unseen. --mutant deferred drops the
deferred-site filter: the suite must then fail on exactly deferred-accepted-not-seen and deferred-retired-not-seen.
--mutant scope drops the D-077 scope check (fails exactly postcert-outside-scope), --mutant drift the root check (fails
exactly postcert-root-drift), --mutant entry the deferral-entry requirement (fails exactly postcert-missing-entry). A
bare --mutant means family.

usage: inventory_families.py [--mutant [family|status|validity|deferred|scope|drift|entry]]
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
DEFERRED_FILTER = "        by_status[status] = [r for r in by_status.get(status, []) if r in refused]\n"
VALIDITY_CHECK = "        if reason not in seen and row.get(\"status\") not in STATUSES:\n"
SCOPE_CHECK = "            if u not in scope:\n"
DRIFT_CHECK = "            elif scope.get(u) not in unit_reasons.get(u, ()):\n"
ENTRY_CHECK = "            if missing:\n"
DEFERRALS = os.path.join(HERE, "post_cert_deferrals.tsv")
COMPLETE = "Inventory complete, with named deferred capabilities"
STATUS_CHECKS = ['    if args.check and by_status.get("implemented-accepted"):\n', "    if args.check and awaiting:\n"]


def row(reason, status="PENDING-OWNER"):
    if isinstance(reason, tuple):
        reason, status = reason
    return "\t".join([reason, "A", "F-test", "legacy", "disposition", "approval", "evidence", "", status]) + "\n"


def report(inventory, work, seen, rows, deferrals=(), execute=()):
    """deferrals: (reason, scope) pairs for post_cert_deferrals.tsv; execute: (entry, [fallback reasons]) pairs."""
    d = os.path.join(work, "collect")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "classify-all.jsonl"), "w") as f:
        f.write(json.dumps({"id": "unit|main.f", "reasons": [{"reason": r} for r in seen]}) + "\n")
    with open(os.path.join(d, "execute.jsonl"), "w") as f:
        for entry, reasons in execute:
            f.write(json.dumps({"corpus": "t", "entry": entry, "rc": None, "executions": [
                {"outcome": "fallback", "reason": r, "path": "main.f"} for r in reasons]}) + "\n")
    tsv = os.path.join(work, "classification.tsv")
    with open(tsv, "w") as f:
        f.write(HEADER + "".join(row(r) for r in rows))
    dtsv = os.path.join(work, "post_cert_deferrals.tsv")
    with open(dtsv, "w") as f:
        f.write("reason\tcapability\tdecision\tscope\trefusal\n")
        for reason, scope in deferrals:
            f.write("\t".join([reason, "a test capability", "D-077 #1 (test)", scope, "program t; first diagnostic: x"]) + "\n")
    md = os.path.join(work, "out.md")
    p = subprocess.run([sys.executable, "-B", inventory, "report", d, "--classification", tsv, "--check",
                        "--deferrals", dtsv, "--markdown", md], capture_output=True, text=True, timeout=120)
    return p.returncode, p.stderr + (open(md).read() if os.path.exists(md) else "")


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
        rc, err = report(inventory, os.path.join(work, "a2"), ["callable:zzz", "method:zzz"],
                         ["method:*", "callable:*"])
        blocked = [l for l in err.split("\n") if "PENDING-OWNER reason(s), not adjudicated" in l]
        if rc != 3 or "G1 PASS" not in err or not blocked or "callable:zzz" not in blocked[0] \
                or "method:zzz" not in blocked[0]:
            failures.append(("family-member-blocks", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "e"), ["type:mul"], [("type:mul", "implemented-accepted")])
        if rc != 3 or "implemented-accepted reason(s) seen again: type:mul" not in err:
            failures.append(("status-accepted-seen", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "f"), ["type:mul"],
                         [("type:mul", "approved"), ("type:div", "implemented-awaiting-review")])
        if rc != 3 or "implemented, awaiting review: type:div" not in err:
            failures.append(("status-awaiting-unseen", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "g"), ["type:mul"],
                         [("type:mul", "approved"), ("type:div", "implemented-accepted")])
        if rc != 0 or "G1 PASS" not in err:
            failures.append(("status-accepted-unseen", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "h"), ["type:mul"],
                         [("type:mul", "approved"), ("type:div", "implemented")])
        if rc != 1 or "invalid status 'implemented' for row type:div" not in err:
            failures.append(("status-invalid-unseen", "rc %d %r" % (rc, err[-300:])))
        # REVIEW-C136-SLOT3 F-A: a slot-3 deferred site (`deferred:<reason>`, the unit runs on the VM) is not a
        # refusal: an implemented-accepted or retired row seen only that way does not block; a real refusal does.
        rc, err = report(inventory, os.path.join(work, "i"), ["deferred:type:mul"], [("type:mul", "implemented-accepted")])
        if rc != 0 or "G1 PASS" not in err or "seen again" in err:
            failures.append(("deferred-accepted-not-seen", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "j"), ["deferred:type:mul"], [("type:mul", "retired")])
        if rc != 0 or "G1 PASS" not in err or "seen again" in err:
            failures.append(("deferred-retired-not-seen", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "k"), ["deferred:type:mul", "type:mul"],
                         [("type:mul", "implemented-accepted")])
        if rc != 3 or "implemented-accepted reason(s) seen again: type:mul" not in err:
            failures.append(("accepted-refusal-still-blocks", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "l"), ["deferred:type:zzz"], ["type:mul"])
        if rc != 1 or "unclassified reason: type:zzz" not in err:
            failures.append(("deferred-needs-row", "rc %d %r" % (rc, err[-300:])))
        rc, err = report(inventory, os.path.join(work, "m"), ["deferred:type:mul"], [("type:mul", "PENDING-OWNER")])
        if rc != 3 or "PENDING-OWNER reason(s), not adjudicated: type:mul" not in err:
            failures.append(("deferred-pending-blocks", "rc %d %r" % (rc, err[-300:])))
        # D-077 #1: deferred-post-cert, its entry and its unit scope.
        pc, unit = ("type:mul", "deferred-post-cert"), [("unit", ["type:mul"])]
        rc, err = report(inventory, os.path.join(work, "n"), ["type:mul"], [pc], [("type:mul", "unit>type:mul")], unit)
        if rc != 0 or COMPLETE not in err or "all supported functionality passes" in err.lower() \
                or "## Named deferred capabilities" not in err or "deferred-post-cert 1" not in err:
            failures.append(("postcert-complete", "rc %d %r" % (rc, err[-400:])))
        rc, err = report(inventory, os.path.join(work, "o"), ["type:mul"], [pc], [("type:mul", "other>type:mul")], unit)
        if rc != 3 or "seen outside their recorded scope: type:mul (unit)" not in err or COMPLETE in err:
            failures.append(("postcert-outside-scope", "rc %d %r" % (rc, err[-400:])))
        rc, err = report(inventory, os.path.join(work, "p"), ["type:mul"], [pc], [("type:mul", "unit>struct-field-init")],
                         unit)
        if rc != 3 or "scope drift" not in err or "recorded struct-field-init" not in err:
            failures.append(("postcert-root-drift", "rc %d %r" % (rc, err[-400:])))
        rc, err = report(inventory, os.path.join(work, "q"), ["type:mul"], [pc], [], unit)
        if rc != 1 or "deferred-post-cert row type:mul has no complete deferral entry" not in err:
            failures.append(("postcert-missing-entry", "rc %d %r" % (rc, err[-400:])))
        rc, err = report(inventory, os.path.join(work, "r"), ["type:mul"], [("type:mul", "approved")],
                         [("type:mul", "unit>type:mul")], unit)
        if rc != 1 or "deferral entry type:mul names a row whose status is 'approved'" not in err:
            failures.append(("postcert-entry-without-row", "rc %d %r" % (rc, err[-400:])))
        rc, err = report(inventory, os.path.join(work, "s"), ["type:mul", "type:div"], [pc, "type:div"],
                         [("type:mul", "unit>type:mul")], [("unit", ["type:mul", "type:div"])])
        if rc != 3 or "PENDING-OWNER reason(s), not adjudicated: type:div" not in err or COMPLETE in err:
            failures.append(("postcert-pending-blocks", "rc %d %r" % (rc, err[-400:])))
    real = []
    entries = {l.split("\t", 1)[0] for l in open(DEFERRALS) if not l.startswith("#") and "\t" in l} - {"reason"}
    for l in open(TSV):
        cells = l.rstrip("\n").split("\t")
        if not l.startswith("#") and len(cells) > 8 and cells[8] == "deferred-post-cert":
            if cells[0] not in entries or "D-077 #1" not in cells[5]:
                real.append(cells[0])
    rows_pc = {l.split("\t", 1)[0] for l in open(TSV) if not l.startswith("#") and l.rstrip("\n").endswith("\tdeferred-post-cert")}
    gates = open(os.path.join(HERE, "..", "approved_refusal.py")).read() + open(os.path.join(HERE, "..", "approved_refusal.sh")).read()
    if real or entries != rows_pc or "deferred-post-cert" in gates:
        failures.append(("real-tsv-post-cert", "rows without entry or D-077 #1 %s; entries without row %s; refusal gates "
                         "mention deferred-post-cert %s" % (real, sorted(entries - rows_pc), "deferred-post-cert" in gates)))
    status = {}
    for l in open(TSV):
        if not l.startswith("#") and "\t" in l:
            cells = l.rstrip("\n").split("\t")
            status[cells[0]] = cells[-1]
    fam = {r: status.get(r) for r in ("builtin-arity:*", "builtin-receiver:*", "method:*", "callable:*")}
    if any(v != "PENDING-OWNER" for v in fam.values()):
        failures.append(("real-tsv-family-rows", "family row statuses %r (each must be PENDING-OWNER)" % fam))
    reasons = [l.split("\t", 1)[0] for l in open(TSV) if not l.startswith("#")]
    if "type:*" in reasons or "type:mul" not in reasons:
        failures.append(("real-tsv-type-rows", "type:* present %s, type:mul present %s" %
                         ("type:*" in reasons, "type:mul" in reasons)))
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mutant", nargs="?", const="family",
                    choices=["family", "status", "validity", "deferred", "scope", "drift", "entry"])
    a = ap.parse_args()
    if not a.mutant:
        failures = cases(INVENTORY)
        for name, why in failures:
            print("FAIL inventory families: %s: %s" % (name, why))
        print("%s inventory families: 23 cases (family rows only for the four D-067 #10 families, never approving unlisted members; the D-069 #4 statuses; the D-077 #1 post-certification deferrals)" %
              ("FAIL" if failures else "PASS"))
        return 1 if failures else 0
    text = open(INVENTORY).read()
    if a.mutant == "family":
        if text.count(EXACT) != 1:
            print("FAIL inventory families mutant: harness: the exact-family code is not in inventory.py (stale mutant)")
            return 1
        mutated, expected = text.replace(EXACT, GENERIC), ["unlisted-type-reason"]
    elif a.mutant == "deferred":
        if text.count(DEFERRED_FILTER) != 1:
            print("FAIL inventory families deferred mutant: harness: the deferred-site filter is not in inventory.py (stale)")
            return 1
        mutated = text.replace(DEFERRED_FILTER, "        pass\n")
        expected = ["deferred-accepted-not-seen", "deferred-retired-not-seen"]
    elif a.mutant in ("scope", "drift", "entry"):
        anchor, expected = {"scope": (SCOPE_CHECK, ["postcert-outside-scope"]),
                            "drift": (DRIFT_CHECK, ["postcert-root-drift"]),
                            "entry": (ENTRY_CHECK, ["postcert-missing-entry"])}[a.mutant]
        if text.count(anchor) != 1:
            print("FAIL inventory families %s mutant: harness: the D-077 check is not in inventory.py (stale)" % a.mutant)
            return 1
        mutated = text.replace(anchor, anchor[:len(anchor) - len(anchor.lstrip())] +
                               ("elif False:\n" if anchor.lstrip().startswith("elif") else "if False:\n"))
    elif a.mutant == "validity":
        if text.count(VALIDITY_CHECK) != 1:
            print("FAIL inventory families validity mutant: harness: the row-status check is not in inventory.py (stale)")
            return 1
        mutated = text.replace(VALIDITY_CHECK, "        if False:\n")
        expected = ["status-invalid-unseen"]
    else:
        if any(text.count(c) != 1 for c in STATUS_CHECKS):
            print("FAIL inventory families status mutant: harness: the status checks are not in inventory.py (stale)")
            return 1
        mutated = text
        for c in STATUS_CHECKS:
            mutated = mutated.replace(c, "    if False:\n")
        expected = ["accepted-refusal-still-blocks", "status-accepted-seen", "status-awaiting-unseen"]
    with tempfile.TemporaryDirectory(prefix="inv-families-mutant-") as work:
        # Same depth below a root as tests/runtime2/cp25/ (inventory.py computes REPO = parents[2]).
        copy = os.path.join(work, "tests", "runtime2", "cp25", "inventory.py")
        os.makedirs(os.path.dirname(copy))
        with open(copy, "w") as f:
            f.write(mutated)
        failures = cases(copy)
    if any("Traceback" in why for _, why in failures):
        print("FAIL inventory families mutant: harness: the mutant copy crashed (not a detection): %r" % failures)
        return 1
    names = sorted(n for n, _ in failures)
    if names != expected:
        print("FAIL inventory families mutant %s: expected only %s to fail, got %s" % (a.mutant, expected, names or "none"))
        return 1
    print("PASS inventory families mutant %s: caught by %s" % (a.mutant, ", ".join(expected)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
