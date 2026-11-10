"""VM-default refusals that a gate may accept (owner 2026-11-10 13:26, item 5; review of C83-C99,
P1-1, P2-1 and P2-2; combined-b3-b5.md sections 85 and 89-90).

A gate may count a VM-default refusal as expected only when:
- the case's own expected reason is pinned by the gate (never read from the VM's output);
- that reason's CP25-R inventory status (tests/runtime2/cp25/classification.tsv, read here, never
  hard-coded) is `approved` or `deferred-named`. A PENDING-OWNER, implement or retired reason, or one
  with no row, is a failure that names the case and the reason;
- under the VM default (NIFT_VM unset) the program is refused with exactly that reason before any
  part of it runs: exit 1, no stdout, the refusal's own lines, and a statistics row with no VM and no
  legacy execution;
- the legacy channel (NIFT_VM=0) still gives the recorded legacy result: check() runs it itself and
  a legacy oracle is mandatory.

  approved_refusal.py status REASON      prints the inventory status (`none` without a row)
  approved_refusal.py self-test --nift N negative controls
"""
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CLASSIFICATION = os.path.join(HERE, "cp25", "classification.tsv")
ACCEPTED = ("approved", "deferred-named")
REFUSAL = re.compile(r"not compiled for the VM \(([^)]*)\)")


def load(path=CLASSIFICATION):
    rows, header = {}, None
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            cells = line.rstrip("\n").split("\t")
            if header is None:
                header = cells
                continue
            row = dict(zip(header, cells))
            rows[row["reason"]] = row
    return rows


PENDING_DIFFERENCES = os.path.join(HERE, "cp25", "pending_differences.tsv")


def pending_differences(gate, path=PENDING_DIFFERENCES):
    """The cases of `gate` listed as unapproved VM/legacy differences (PENDING-OWNER). A listed case still
    fails its gate; the list only lets the gate name the case as a recorded open item."""
    cases, header = set(), None
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            cells = line.rstrip("\n").split("\t")
            if header is None:
                header = cells
                continue
            row = dict(zip(header, cells))
            if row["gate"] == gate:
                cases.add(row["case"])
    return cases


def row_for(reason, rows=None):
    """The exact row, else the family row `<prefix>:*` (as inventory.py)."""
    rows = load() if rows is None else rows
    if reason in rows:
        return rows[reason]
    return rows.get(reason.split(":")[0] + ":*") if ":" in reason else None


def status(reason, rows=None):
    row = row_for(reason, rows)
    return row.get("status") if row else None


def acceptance_problem(case, reason, rows=None):
    """None when `reason` may be accepted as the refusal of `case`; else the failure text."""
    st = status(reason, rows)
    if st is None:
        return "%s: refused with %s, which has no CP25-R inventory row" % (case, reason)
    if st not in ACCEPTED:
        return "%s: refused with %s, whose inventory status is %s (only %s may be accepted)" % (
            case, reason, st, " or ".join(ACCEPTED))
    return None


def pinned_problem(case, pins, observed, rows=None):
    """None when the VM-default refusal `observed` of `case` is the reason pinned for it in `pins` and that
    reason may be accepted; else the failure. A case without a pin fails whatever the reason, so a case
    that runs today and becomes refused, even with an approved reason, is caught (review of C83-C99, P2-1)."""
    pinned = pins.get(case)
    if pinned is None:
        return "%s: refused with %s under the VM default, but no refusal is pinned for this case" % (case, observed)
    if observed != pinned:
        return "%s: refused with %s, pinned %s" % (case, observed, pinned)
    return acceptance_problem(case, pinned, rows)


def refusal_reason(stderr):
    m = REFUSAL.search(stderr or "")
    return m.group(1) if m else None


def _run(argv, cwd, stdin, env, mode):
    with tempfile.TemporaryDirectory(prefix="approved-refusal-") as d:
        stats = os.path.join(d, "stats.jsonl")
        e = {k: v for k, v in (env or os.environ).items() if k not in ("NIFT_VM", "NIFT_VM_STATS")}
        e["NIFT_VM_STATS"] = stats
        if mode is not None:
            e["NIFT_VM"] = mode
        p = subprocess.run(argv, cwd=cwd, input=stdin, capture_output=True, text=True, env=e, timeout=60)
        rows = [json.loads(x) for x in open(stats).read().splitlines() if x.strip()] if os.path.exists(stats) else []
    return p, (rows[-1] if rows else {})


def check(case, argv, cwd, reason, legacy, stdin=None, env=None, rows=None):
    """None when `case` is refused as accepted under the VM default and legacy still matches; else the
    problem. `reason` is the case's pinned expected reason. `legacy` is mandatory: a function taking the
    legacy channel's CompletedProcess and returning None or a problem text, or a recorded
    (exit, stdout) or (exit, stdout, stderr) tuple."""
    if legacy is None:
        return "%s: no legacy oracle (a recorded legacy result is mandatory)" % case
    problem = acceptance_problem(case, reason, rows)
    if problem:
        return problem
    p, row = _run(argv, cwd, stdin, env, None)
    if (p.returncode != 1 or p.stdout != "" or refusal_reason(p.stderr) != reason
            or "no part of it ran" not in p.stderr or row.get("reason") != reason
            or row.get("vm_executions") != 0 or row.get("legacy_executions") != 0):
        return "%s: not the refusal %s (%s) before execution: exit %d stdout %r stderr %r stats %r" % (
            case, reason, status(reason, rows), p.returncode, p.stdout, p.stderr[:300],
            {k: row.get(k) for k in ("outcome", "reason", "vm_executions", "legacy_executions")})
    q, _ = _run(argv, cwd, stdin, env, "0")
    if callable(legacy):
        lp = legacy(q)
        return "%s: legacy channel (NIFT_VM=0): %s" % (case, lp) if lp else None
    got = (q.returncode, q.stdout, q.stderr)[:len(legacy)]
    if got != tuple(legacy):
        return "%s: legacy channel (NIFT_VM=0): got %r, recorded %r" % (case, got, tuple(legacy))
    return None


def self_test(nift):
    """Negative controls: each must fail."""
    failures = 0
    rows = load()
    with tempfile.TemporaryDirectory(prefix="approved-refusal-self-") as d:
        def program(name, text):
            path = os.path.join(d, name)
            with open(path, "w") as f:
                f.write(text)
            return [nift, path]
        key = program("key.f", 'h := set()\nm := map()\nm.set(h, 1)\nprint(m.size())\n')
        legacy_key = (0, "1\n")
        controls = [
            # the accepted case itself must pass (positive control)
            ("positive: collection-mutable-key (approved)", lambda: check("key", key, d, "collection-mutable-key", legacy_key), True),
            ("a PENDING-OWNER reason", lambda: check("len", program("len.f", 'print(len("abc"))\n'), d, "callable:len",
                                                    (1, "")), False),
            ("an unknown reason (no row)", lambda: acceptance_problem("x", "no-such-reason:at-all", rows), False),
            ("an implement reason", lambda: acceptance_problem("x", "builtin-bytes:bytes", rows), False),
            ("a missing legacy oracle", lambda: check("key", key, d, "collection-mutable-key", None), False),
            ("a wrong legacy oracle", lambda: check("key", key, d, "collection-mutable-key", (0, "2\n")), False),
            ("a wrong pinned reason", lambda: check("key", key, d, "callable:timer", legacy_key), False),
            ("a running program", lambda: check("run", program("run.f", 'print(1)\n'), d, "callable:timer", (0, "1\n")), False),
            ("an unpinned case refused with an approved reason",
             lambda: pinned_problem("stat-like", {}, "collection-mutable-key", rows), False),
            ("a case refused with another approved reason than its pin",
             lambda: pinned_problem("key", {"key": "callable:timer"}, "collection-mutable-key", rows), False),
            ("positive: a pinned accepted reason", lambda: pinned_problem("key", {"key": "collection-mutable-key"},
                                                                        "collection-mutable-key", rows), True),
        ]
        for name, run, should_pass in controls:
            problem = run()
            ok = (problem is None) == should_pass
            print("%s self-test %s: %s" % ("PASS" if ok else "FAIL", name, problem or "accepted"))
            failures += not ok
    print("%s approved_refusal self-test: %d control(s), %d failure(s)" % ("PASS" if not failures else "FAIL",
                                                                          len(controls), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "status":
        print(status(sys.argv[2]) or "none")
        sys.exit(0)
    if len(sys.argv) == 4 and sys.argv[1] == "self-test" and sys.argv[2] == "--nift":
        sys.exit(self_test(os.path.abspath(sys.argv[3])))
    sys.exit(__doc__)
