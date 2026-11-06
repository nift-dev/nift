#!/usr/bin/env python3
"""B4b 23: the compile-scaling gate on deterministic analysis-work counts
(docs/runtime-2/checkpoints/b4b-element-types.md section 15).

  work.py --harness .build/rt/b4b-work            the gate (against work_b4a.json)
  work.py --harness H --measure > counts.json     measure H (e.g. a harness built by work_build.sh)
  work.py --self-test                             the gate's rules on the recorded measurements

The shapes are the compile-scaling shapes of b4b_types.py (`glob`, `many`, `nestb`, `containers`)
at 100, 400, 800 and 1,600 helpers. The harness (tests/rt/b4b_work.cpp) plans and infers each
exactly as the compiler does and prints the inference's work counts (UnitKinds::work). The
work W is the sum of the elementary operations: nodes walked, enqueues, site reads computed,
site unions, reader moves, site entries mapped to classes and type-union entries; B4a's
analysis has the first two and the type unions (no sites).

The budget is derived from B4a's measured counts (work_b4a.json, measured with work_build.sh at
0df0df6c), never from the tip's:
  converged  every shape reaches its fixed point (no walk-budget refusal)
  growth     from each size to the next the tip's W grows by at most 1.10x B4a's growth of its W
             over the same step (B4a's analysis is linear on every shape: its own growth is the
             reference; a quadratic term fails at the first step)
  walks      the tip walks at most B4a's walks plus one walk per unit: the one re-walk of every
             reader when the shape's one site class widens past 16 origins (F1); `containers`
             has no widening and B4a's walks are its bound
The ratio W(tip) / W(B4a) is printed for each size (guidance; the 1.5x compile-time objective is
measured on Linode with repeated wall-clock runs).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import b4b_types  # noqa: E402  (the shapes)

SIZES = (100, 400, 800, 1600)
SHAPES = (("glob", b4b_types.glob, 1), ("many", b4b_types.many, 1), ("nestb", b4b_types.nestb, 1),
          ("containers", b4b_types.containers, 0))  # the number of widenings of each shape
FIELDS = ("nodes", "enqueues", "materializations", "site_unions", "reader_moves", "site_items", "unite_items")
GROWTH = 1.10


def work(row):
    return sum(int(row.get(f, 0)) for f in FIELDS)


def measure(harness):
    out = {}
    d = tempfile.mkdtemp(prefix="b4b-work-")
    try:
        files = []
        for name, make, _ in SHAPES:
            for n in SIZES:
                path = os.path.join(d, "%s%d.f" % (name, n))
                open(path, "w").write(make(n))
                files.append((name, n, path))
        p = subprocess.run([harness] + [f for _, _, f in files], capture_output=True, text=True, timeout=600)
        rows = [json.loads(line) for line in p.stdout.splitlines() if line.strip()]
        if p.returncode != 0 or len(rows) != len(files):
            sys.exit("work.py: the harness failed (exit %d): %s" % (p.returncode, p.stderr[-500:]))
        for (name, n, _), row in zip(files, rows):
            row.pop("file", None)
            out.setdefault(name, {})[str(n)] = row
    finally:
        for f in os.listdir(d):
            os.unlink(os.path.join(d, f))
        os.rmdir(d)
    return out


def gate(tip, base, label="measured", quiet=False):
    """The failures of `tip` against the B4a counts `base` (an empty list: within the budget)."""
    failures = []
    for name, _, widenings in SHAPES:
        t, b = tip[name], base[name]
        line = []
        for n in SIZES:
            r, s = t[str(n)], b[str(n)]
            if not r.get("converged"):
                failures.append("%s %s %d: not converged after %d walks" % (label, name, n, r["walks"]))
            bound = s["walks"] + widenings * r["units"]
            if r["walks"] > bound:
                failures.append("%s %s %d: %d walks, B4a %d + %d per widening (%d)" %
                                (label, name, n, r["walks"], s["walks"], r["units"], bound))
            line.append("%d: W %d (B4a %d, %.2fx), walks %d (B4a %d)" %
                        (n, work(r), work(s), work(r) / work(s), r["walks"], s["walks"]))
        for small, large in zip(SIZES, SIZES[1:]):
            grow_t = work(t[str(large)]) / work(t[str(small)])
            grow_b = work(b[str(large)]) / work(b[str(small)])
            if grow_t > GROWTH * grow_b:
                failures.append("%s %s %d->%d: W grows %.2fx, B4a %.2fx (bound %.2fx)" %
                                (label, name, small, large, grow_t, grow_b, GROWTH * grow_b))
        if not quiet:
            print("work %s %s: %s" % (label, name, "; ".join(line)))
    return failures


def self_test():
    """The rules on the recorded measurements (work_history.json, work_build.sh at each commit): B4a
    against itself and the B4b tips accepted for these counts pass; the two recorded pathologies
    fail (a42035c1: `many` never converges, the F1 walks; `nestb` superlinear, the type unions of
    the previous round; e293105e: superlinear on every shape but `containers`), and the mutant A30
    (the F1 oscillation restored) fails."""
    history = json.load(open(os.path.join(HERE, "work_history.json")))
    base = json.load(open(os.path.join(HERE, "work_b4a.json")))["counts"]
    bad = 0
    expect = {"0df0df6c": [], "16f24f36": [], "a42035c1": ["many", "nestb"], "e293105e": ["glob", "many", "nestb"],
              "mutant-A30": ["many"]}
    for commit, shapes in expect.items():
        failures = gate(history[commit], base, commit, quiet=True)
        failed = sorted({f.split()[1] for f in failures})
        ok = failed == sorted(shapes)
        bad += not ok
        print("%s %s: fails %s (expected %s)%s" % ("PASS" if ok else "FAIL", commit, failed or "nothing",
                                                   shapes or "nothing", "" if ok else ": " + "; ".join(failures)))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness")
    ap.add_argument("--baseline", default=os.path.join(HERE, "work_b4a.json"))
    ap.add_argument("--measure", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        bad = self_test()
        print("%s work gate self-test" % ("FAIL" if bad else "PASS"))
        return 1 if bad else 0
    counts = measure(os.path.abspath(args.harness))
    if args.measure:
        json.dump(counts, sys.stdout, indent=1, sort_keys=True)
        print()
        return 0
    failures = gate(counts, json.load(open(args.baseline))["counts"])
    for f in failures:
        print("FAIL " + f)
    print("%s b4b work: %d shapes x %d sizes, %d failure(s)" % ("FAIL" if failures else "PASS", len(SHAPES),
                                                              len(SIZES), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
