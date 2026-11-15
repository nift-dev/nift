#!/usr/bin/env python3
"""Case names of read_limit.py against certify's case-ID table (review of the cert-prep work, C4 / F-P2-1).

scripts/certify_fingerprints.py maps a `FAIL <name> <detail>` line of read_limit.py to a case ID with
READ_LIMIT_TABLE: an entry matches a prefix of the line that is followed by a space. A tag appended to an original
name ("<name> [legacy channel]") would therefore map onto the original case's ID, so a failing legacy-channel or
refusal-contract case could stand in for, or collide with, the original VM case in a baseline comparison.

read_limit.py puts every tag first (LEGACY_CHANNEL, REFUSAL_CONTRACT, VM_EQUIVALENT). This selftest takes every case name of the
recorded post-repair run (docs/runtime-2/evidence/host-deps-d077/), has read_limit.py's own check() and
refusal_contract() print it in each tagged form (review L-a: the names come from the code path the suite uses), and
maps each FAIL line with certify's own fingerprint function (certify_fingerprints.fingerprint):
  - no tagged line maps to more than one entry (it would be UNMAPPED as ambiguous);
  - no tagged line maps to the ID of an untagged (original) case;
  - the legacy-channel and refusal-contract forms of one case never share an ID;
  - negative control: the old suffix form does collide with an original ID (the check can fail).
A tagged line that maps to no entry is UNMAPPED, which certify reports on its own text and blocks: allowed.

Needs scripts/certify_fingerprints.py (a full checkout); without it the selftest is NOT RUN (exit 2).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
LOG = os.path.join(REPO, "docs", "runtime-2", "evidence", "host-deps-d077", "after-read-limit-pinned-c137.txt")
OLD_SUFFIXES = (" [legacy channel, NIFT_VM=0]",)
OLD_CONTRACT = re.compile(r" \[VM default: refusal contract, .*\]$")


def main():
    if not os.path.exists(os.path.join(REPO, "scripts", "certify_fingerprints.py")):
        print("NOT RUN read_limit names selftest: scripts/certify_fingerprints.py is not in this checkout")
        return 2
    sys.path.insert(0, os.path.join(REPO, "scripts"))
    sys.path.insert(0, HERE)
    import certify_fingerprints as F  # noqa: E402
    import read_limit as rl  # noqa: E402

    import contextlib  # noqa: E402
    import io  # noqa: E402
    import types  # noqa: E402

    def ident(fail_line):
        """certify's own case ID for one FAIL line of read_limit.py (certify_fingerprints.fingerprint)."""
        fp = F.fingerprint("test-runtime2-host-deps", "NIFT_BIN=x python3 tests/runtime2/read_limit.py\n" + fail_line + "\n")
        (step, cid, kind, _exit, detail), = fp["cases"].keys()
        if cid == "UNMAPPED":
            n = re.search(r"\((\d+) table entries match\)$", detail)
            return ("AMBIGUOUS",) if n and int(n.group(1)) > 1 else None
        return cid

    def emitted(call):
        """The FAIL line read_limit.py's own check() path prints for `call` (review L-a: no hand-built names)."""
        buf, before = io.StringIO(), len(rl.failures)
        with contextlib.redirect_stdout(buf):
            call()
        del rl.failures[before:]
        return next(l for l in buf.getvalue().splitlines() if l.startswith("FAIL "))

    def legacy(name):
        rl.CHANNEL.update(legacy=True, record=None, pending=[])
        try:
            return emitted(lambda: rl.check(name, False, "detail"))
        finally:
            rl.CHANNEL.update(legacy=False, record=None, pending=[])

    def contract(name, reason):
        token = rl.DEFERRED[reason][2]
        fake = types.SimpleNamespace(returncode=0, stdout="ran", stderr="")
        return emitted(lambda: rl.refusal_contract(name, reason, fake, "x := %s1)\n" % token))

    originals = set()
    for line in open(LOG):
        if not line.startswith(("PASS ", "FAIL ")):
            continue
        name = line.rstrip("\n").split(" ", 1)[1]
        name = OLD_CONTRACT.sub("", name)
        for s in OLD_SUFFIXES:
            name = name.replace(s, "")
        if name.startswith("[") or "printed, measured" in name or name.startswith("read(limit/2)"):
            continue
        originals.add(name)
    original_ids = {ident(emitted(lambda n=n: rl.check(n, False, "detail"))) for n in originals} - {None}
    failures, mapped, unmapped = [], 0, 0
    for name in sorted(originals):
        forms = {"legacy": legacy(name)}
        forms.update({"contract " + r: contract(name, r) for r in rl.DEFERRED})
        ids = {}
        for kind, line in forms.items():
            i = ident(line)
            if i == ("AMBIGUOUS",):
                failures.append("%s maps to several entries" % line)
            elif i in original_ids:
                failures.append("%s maps onto an original case ID %s" % (line, i))
            elif i:
                ids[kind] = i
                mapped += 1
            else:
                unmapped += 1
        if len(set(ids.values())) != len(ids):
            failures.append("tagged forms of %r share an ID: %s" % (name, ids))
    for name in rl.EQUIVALENT_NAMES:
        i = ident(emitted(lambda n=name: rl.check(rl.VM_EQUIVALENT + n, False, "detail")))
        if i and (i == ("AMBIGUOUS",) or i in original_ids):
            failures.append("%s%s collides: %s" % (rl.VM_EQUIVALENT, name, i))
    # Negative control: the suffix form of 2026-11-12 (P2) maps onto original IDs.
    collided = sum(1 for n in originals if ident("FAIL %s%s detail" % (n, OLD_SUFFIXES[0])) in original_ids)
    if not collided:
        failures.append("negative control: the old suffix form collided with no original ID (the check is vacuous)")
    for f in failures:
        print("FAIL read_limit names selftest:", f)
    print("%s read_limit names selftest: %d original names, %d tagged forms mapped to new IDs, %d UNMAPPED; the old "
          "suffix form collides for %d (negative control)" %
          ("FAIL" if failures else "PASS", len(originals), mapped, unmapped, collided))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
