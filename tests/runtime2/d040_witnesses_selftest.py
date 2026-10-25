#!/usr/bin/env python3
"""Negative controls for the exact D-040 witnesses (d040_witnesses.py).

Each witness must be accepted with its exact source, triples and VM
statistics, and everything else must be rejected: an unrelated difference, a
lookalike source, any changed stdout/stderr/exit on either side, a refusal,
a replay or a legacy execution. The pinned hashes must also still match the
programs the harnesses actually run, so drift fails loudly instead of the
witness silently matching nothing. Runs through vm_policies.classify too, the
classifier every differential harness uses. No binary is needed.
"""
import hashlib
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import d040_witnesses as W  # noqa: E402
import vm_policies  # noqa: E402

GOOD = {"outcome": "vm", "vm_executions": 1, "legacy_executions": 0}
failures = 0


def check(label, ok):
    global failures
    print(("PASS " if ok else "FAIL ") + label)
    failures += 0 if ok else 1


def sources():
    """The exact programs the harnesses run, keyed by witness id."""
    out = {}
    for wid, rel in (("vm-corpus/cp11/replay_alias.f", "tests/runtime2/vm-corpus/cp11/replay_alias.f"),
                     ("semantics/alias_place.f", "tests/runtime2/semantics/alias_place.f")):
        out[wid] = open(os.path.join(REPO, rel), "rb").read()
    import vm_adversarial_gen
    for _family, name, src, _extra in vm_adversarial_gen.alias(0, 0):
        if name in ("alias-decl-5-y.push(2)-top", "alias-decl-5-y.push(2)-fn-top"):
            out["adversarial " + name] = src.encode()
    spec = importlib.util.spec_from_file_location("vm_review_cp12", os.path.join(HERE, "vm_review_cp12.py"))
    review = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(review)
    for _group, name, src, _reason in review.programs():
        if name in ("chain199-alias", "chain201-alias"):
            out["review D1 " + name] = src.encode()
    return out


def main():
    srcs = sources()
    check(f"every witness has its harness program ({len(srcs)} of {len(W.WITNESSES)})",
          sorted(srcs) == sorted(w[0] for w in W.WITNESSES))
    for wid, sha, legacy, vm, _where in W.WITNESSES:
        src = srcs.get(wid, b"")
        check(f"{wid}: pinned hash equals the program the harness runs", hashlib.sha256(src).hexdigest() == sha)
        check(f"{wid}: exact witness accepted", W.match(src, legacy, vm, GOOD) == wid)
        st = dict(GOOD, _source=src)
        check(f"{wid}: vm_policies.classify approves it as D-040",
              vm_policies.classify(legacy, vm, st, src) == ("approved", "D-040 independent copy"))
        # Lookalikes: one field changed at a time; each must be rejected.
        lookalikes = {
            "VM extra stdout line": (legacy, (vm[0] + b"x\n", vm[1], vm[2]), GOOD, src),
            "VM legacy-identical stdout": (legacy, legacy, GOOD, src),
            "VM stderr added": (legacy, (vm[0], b"warning\n", vm[2]), GOOD, src),
            "VM exit changed": (legacy, (vm[0], vm[1], 1), GOOD, src),
            "legacy stdout changed": ((legacy[0] + b"y\n", legacy[1], legacy[2]), vm, GOOD, src),
            "legacy exit changed": ((legacy[0], legacy[1], 1), vm, GOOD, src),
            "refused (fallback)": (legacy, vm, dict(GOOD, outcome="fallback"), src),
            "replayed (two VM executions)": (legacy, vm, dict(GOOD, vm_executions=2), src),
            "legacy execution": (legacy, vm, dict(GOOD, legacy_executions=1), src),
            "vm-error outcome": (legacy, vm, dict(GOOD, outcome="vm-error"), src),
            "lookalike source (one byte)": (legacy, vm, GOOD, src + b"\n"),
            "no statistics": (legacy, vm, None, src),
        }
        for label, (lg, v, s, source) in lookalikes.items():
            if label == "VM legacy-identical stdout":
                ok = W.match(source, lg, v, s) is None  # identical runs are 'same', never a D-040 witness
            else:
                ok = W.match(source, lg, v, s) is None and \
                    vm_policies.classify(lg, v, dict(s, _source=source) if s else s, source)[1] != "D-040 independent copy"
            check(f"{wid}: rejects {label}", ok)
    # An unrelated alias-shaped difference with D-040-looking output is not approved.
    unrelated = b"a := [1, 2]\nc := a\nc.push(3)\nprint(a.length())\n"
    check("unrelated alias program with witness output rejected",
          vm_policies.classify((b"3\n", b"", 0), (b"2\n", b"", 0), dict(GOOD, _source=unrelated), unrelated)
          == ("diff", None))
    other = b"print(1)\n"
    check("unrelated difference rejected",
          vm_policies.classify((b"1\n", b"", 0), (b"2\n", b"", 0), dict(GOOD, _source=other), other) == ("diff", None))
    print(("PASS" if not failures else "FAIL") + f" D-040 witnesses: {len(W.WITNESSES)} exact witnesses, "
          f"{failures} control failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
