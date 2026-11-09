#!/usr/bin/env python3
"""B04 (owner approval 2026-11-09 17:31): the D4 comparator-sort expectations under the approved
copy-sort-commit semantics (owner ruling Q7, 2026-11-07, docs/runtime-2/compatibility.md: a
comparator sort sorts a copy and commits it once; aliases see the original order; a failure leaves
the receiver unchanged; identity is kept).

The frozen D4 traces (fixtures/d4-traces.json.gz, sha256 pinned in fixtures/SHA256.json) record the
superseded in-place semantics: each comparator call's view of the receiver is the partially sorted
array, and a failed sort leaves a permutation. They stay unchanged as historical evidence. This
generator derives fixtures/d4-q7-expectations.json.gz from them mechanically, per case:

  copied exactly from the frozen record: every comparator call's pair (l, r), in order; the call
      count; the failure flag (the sort stopped by the comparator's `throw error("stop")`, printed
      as `Estop`; the D4 programs print no position for it)
  from the Q7 model: each call's receiver view is the original array (the case's values, as the D4
      program builds them); the final array is the frozen final when the sort completed, and the
      original array when it failed

Nothing else changes. The file is gzip-compressed; its content's sha256 (d4_q7_uncompressed) and
the file's own (d4-q7-expectations.json.gz) are pinned in fixtures/SHA256.json. `--check`
regenerates the content in memory and requires the checked-in content to be byte-identical and both
pins to hold; the cp14b gate (sort_replay.py) runs that check first and compares every field.

usage: q7_expectations.py [--check] [--frozen F] [--out F]
"""
import argparse
import gzip
import hashlib
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / "fixtures"
FROZEN = FIXTURES / "d4-traces.json.gz"
DERIVED = FIXTURES / "d4-q7-expectations.json.gz"
PINS = FIXTURES / "SHA256.json"
RULING = "owner ruling Q7 (2026-11-07): copy-sort-commit; approved for these expectations 2026-11-09 17:31"


def original(case):
    """the receiver as the D4 program builds it (d4_validate.nift_source)"""
    return [dict(k=v, id=i) for i, v in enumerate(case["values"])] if case["records"] else list(case["values"])


def derive(frozen_bytes):
    frozen = json.loads(gzip.decompress(frozen_bytes))
    cases = []
    for record in frozen["results"]:
        case, native = record["case"], record["native"]
        before = original(case)
        cases.append({
            "case": case["id"],
            "expected": {
                "trace": [{"l": call["l"], "r": call["r"], "a": before} for call in native["trace"]],
                "final": before if native["failed"] else native["final"],
                "calls": native["calls"],
                "failed": native["failed"],
            },
        })
    return {
        "schema": "nift-cp14b-d4-q7/1",
        "ruling": RULING,
        "frozen": FROZEN.name,
        "frozen_sha256": hashlib.sha256(frozen_bytes).hexdigest(),
        "generator": "tests/runtime2/cp14b/q7_expectations.py",
        "cases": cases,
    }


def render(data):
    """the canonical content (its sha256 is pinned as d4_q7_uncompressed)"""
    return (json.dumps(data, sort_keys=True, separators=(",", ":")) + "\n").encode()


def verify_frozen(frozen_bytes):
    pins = json.loads(PINS.read_text())
    problems = []
    if hashlib.sha256(frozen_bytes).hexdigest() != pins.get("d4-traces.json.gz"):
        problems.append("the frozen D4 traces differ from their pinned sha256 (fixtures/SHA256.json)")
    if hashlib.sha256(gzip.decompress(frozen_bytes)).hexdigest() != pins.get("d4_uncompressed"):
        problems.append("the frozen D4 traces' content differs from its pinned sha256")
    return problems


def check_derived(path=DERIVED, frozen=FROZEN):
    """[] when the checked-in derived file is exactly what the generator makes from the pinned
    frozen traces, and its own sha256 is the pinned one; otherwise the problems."""
    frozen_bytes = Path(frozen).read_bytes()
    problems = verify_frozen(frozen_bytes)
    want = render(derive(frozen_bytes))
    try:
        packed = Path(path).read_bytes()
        have = gzip.decompress(packed)
    except (OSError, EOFError, gzip.BadGzipFile) as e:
        return problems + [f"cannot read {path}: {e}"]
    if have != want:
        problems.append(f"{path} is not what q7_expectations.py derives from the frozen traces (regenerate)")
    pins = json.loads(PINS.read_text())
    if hashlib.sha256(have).hexdigest() != pins.get("d4_q7_uncompressed"):
        problems.append("the derived Q7 expectations' content differs from its pinned sha256 (fixtures/SHA256.json)")
    if hashlib.sha256(packed).hexdigest() != pins.get("d4-q7-expectations.json.gz"):
        problems.append("the derived Q7 expectations file differs from its pinned sha256 (fixtures/SHA256.json)")
    return problems


def load(path=DERIVED):
    return json.loads(gzip.decompress(Path(path).read_bytes()))


def self_test():
    """negative controls: a derived file changed in any field (a call's pair or receiver view, the
    final array, the call count, the failure flag) is refused by check_derived"""
    import tempfile
    base = load()
    tampers = {
        "pair": lambda e: e["cases"][100]["expected"]["trace"][0].update(l=999),
        "view": lambda e: e["cases"][100]["expected"]["trace"][0].update(a=[1]),
        "final": lambda e: e["cases"][100]["expected"].update(final=[]),
        "calls": lambda e: e["cases"][100]["expected"].update(calls=0),
        "failed": lambda e: e["cases"][100]["expected"].update(failed=True),
    }
    bad = []
    with tempfile.TemporaryDirectory(prefix="q7-self-test-") as tmp:
        for name, tamper in tampers.items():
            changed = json.loads(json.dumps(base))
            tamper(changed)
            path = Path(tmp) / f"{name}.json.gz"
            path.write_bytes(gzip.compress(render(changed), mtime=0))
            if not check_derived(path):
                bad.append(name)
    for name in bad:
        print(f"FAIL q7 expectations self-test: a changed {name} was accepted")
    if not bad:
        print(f"PASS q7 expectations self-test: {len(tampers)} changed fields refused")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--frozen", default=str(FROZEN))
    ap.add_argument("--out", default=str(DERIVED))
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if a.check:
        problems = check_derived(a.out, a.frozen)
        for p in problems:
            print("FAIL q7 expectations: " + p)
        if not problems:
            print(f"PASS q7 expectations: {a.out} derived from the pinned frozen traces, sha256 pinned")
        return 1 if problems else 0
    frozen_bytes = Path(a.frozen).read_bytes()
    problems = verify_frozen(frozen_bytes)
    if problems:
        for p in problems:
            print("FAIL q7 expectations: " + p)
        return 1
    data = render(derive(frozen_bytes))
    packed = gzip.compress(data, compresslevel=9, mtime=0)
    Path(a.out).write_bytes(packed)
    print(f"wrote {a.out}: {len(json.loads(data)['cases'])} cases, content sha256 {hashlib.sha256(data).hexdigest()}, "
          f"file sha256 {hashlib.sha256(packed).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
