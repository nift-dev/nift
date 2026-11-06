#!/usr/bin/env python3
"""B4b standing guard-off gate, the cross-check part (owner ruling, review L4).

  guardoff.py --guardoff NIFT_GUARDOFF --nift NIFT [--dirs D ...] [--twice N]

For every script fixture under the given tests/runtime2 directories (run in a fresh
temporary copy of its own directory, or of its parent when the script names `../`, at the
same absolute path for every run of it, so the source tree is never written; review F2), on
the guard-off build (the B2 reference-probe compiler:
no Phase A guard, shared references) it fails on:
  - exit 70, a signal or crash (exit >= 128, or a negative status), a timeout;
  - leaked internal diagnostics or tags (`compiler defect`, `internal failure`, `\\x1fnift:`,
    `\\u001fnift:`, `unexpected operands`);
  - a nonzero dispatch-defect counter (NIFT_VM_STATS);
  - output different from the guard-on build where the guard-on build admits the program too
    (the guard admits only programs whose output is the same under copies and shared
    references, so B5 does not change them), except the approved list
    (tests/runtime2/b4b/guardoff/approved_changes.txt);
  - nondeterminism: the first N programs run twice on the guard-off build must agree.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
HERE = os.path.dirname(os.path.abspath(__file__))
INTERNAL = re.compile(r"compiler defect|internal failure|unexpected operands|\x1fnift:|\\u001fnift:|cell reached")


WORK = [None]  # one temporary root per process: every run's copy is made at the same path


def copy_of(path):
    """A fresh copy of the script's directory (its parent's when the script names `../`) under
    WORK; returns the copied script's directory."""
    src = os.path.dirname(path)
    with open(path, encoding="utf-8", errors="replace") as f:
        base = os.path.dirname(src) if "../" in f.read() else src
    tree = os.path.join(WORK[0], "tree")
    shutil.rmtree(tree, ignore_errors=True)
    shutil.copytree(base, os.path.join(tree, os.path.basename(base)), symlinks=True)
    return os.path.join(tree, os.path.basename(base), os.path.relpath(src, base))


def run(nift, path, mode="require"):
    d = tempfile.mkdtemp(prefix="b4b-guardoff-")
    st = os.path.join(d, "s.jsonl")
    env = {"PATH": "/usr/bin:/bin", "HOME": d, "TMPDIR": d, "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
           "NO_COLOR": "1", "NIFT_VM": mode, "NIFT_VM_STATS": st}
    try:
        p = subprocess.run(["timeout", "30", nift, os.path.basename(path)], cwd=copy_of(path), env=env,
                           stdin=subprocess.DEVNULL, capture_output=True)
        rows = []
        if os.path.exists(st):
            for line in open(st):
                try:
                    rows.append(json.loads(line))
                except ValueError:
                    pass
        return p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"), p.returncode, rows
    finally:
        shutil.rmtree(d, ignore_errors=True)


def admitted(rows):
    return bool(rows) and all(r.get("outcome") in ("vm", "vm-error") for r in rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--guardoff", required=True)
    ap.add_argument("--nift", required=True)
    ap.add_argument("--dirs", nargs="+", default=["."])
    ap.add_argument("--twice", type=int, default=300)
    args = ap.parse_args()
    args.guardoff, args.nift = os.path.abspath(args.guardoff), os.path.abspath(args.nift)
    WORK[0] = tempfile.mkdtemp(prefix="b4b-guardoff-copies-")
    try:
        return cross_check(args)
    finally:
        shutil.rmtree(WORK[0], ignore_errors=True)


def cross_check(args):
    approved = {}
    for line in open(os.path.join(HERE, "guardoff", "approved_changes.txt")):
        if line.strip() and not line.startswith("#"):
            approved[line.split()[0]] = line.split()[1]
    files = []
    for d in args.dirs:
        for dp, dn, fn in os.walk(os.path.join(ROOT, "tests", "runtime2", d)):
            dn.sort()
            files += [os.path.join(dp, f) for f in sorted(fn) if f.endswith(".f") and not dp.endswith("/modules")]
    failures, compared, both, changed_approved = [], 0, 0, 0
    for k, path in enumerate(files):
        rel = os.path.relpath(path, os.path.join(ROOT, "tests", "runtime2"))
        go, ge, gr, grows = run(args.guardoff, path)
        problems = []
        allowed = approved.get(rel)
        if (gr == 70 and allowed != "exit70") or (gr == 124 and allowed != "timeout") or gr > 124 or gr < 0 or \
                gr in (125, 126, 127):
            problems.append("exit %d" % gr)
        if INTERNAL.search(go + ge) and not (allowed == "exit70" and "memory budget exceeded" in ge):
            problems.append("internal text or tag")
        if sum(int(r.get("dispatch_defects", 0)) for r in grows):
            problems.append("dispatch defects")
        if k < args.twice and allowed != "output" and run(args.guardoff, path)[:3] != (go, ge, gr):
            problems.append("nondeterministic")
        if admitted(grows):
            no, ne, nr, nrows = run(args.nift, path)
            if admitted(nrows):
                both += 1
                if (no, ne, nr) != (go, ge, gr):
                    if allowed == "output":
                        changed_approved += 1
                    else:
                        problems.append("guard-on output differs: %r/%r/%d vs %r/%r/%d" %
                                        (no[-120:], ne[-160:], nr, go[-120:], ge[-160:], gr))
        compared += 1
        if problems:
            failures.append((rel, problems))
    for rel, problems in failures[:40]:
        print("FAIL %s: %s" % (rel, "; ".join(problems)))
    print("guard-off cross-check: %d scripts, %d admitted by both builds (%d approved changes), %d failure(s)"
          % (compared, both, changed_approved, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
