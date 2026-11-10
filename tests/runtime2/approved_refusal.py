"""Already-approved VM-default refusals (owner 2026-11-10 13:26, item 5; combined-b3-b5.md section 81).

APPROVED maps each refusal reason the owner has approved as a release behaviour to its decision.
check() requires, under the VM default (NIFT_VM unset), exactly that refusal before any part of
the program runs: exit 1, no stdout, the refusal's own lines, and a statistics row with no VM or
legacy execution. The caller keeps checking the legacy channel (NIFT_VM=0) against its own oracle.
"""
import json
import os
import re
import subprocess
import tempfile

APPROVED = {
    "fn-name-reserved": "D-009 #3",
    "free-name": "owner ruling 2026-10-29 (keep refused)",
    "fn-flags": "D-047 #1 / D-048 #1",
    "collection-mutable-key": "D-NEXT-refsem #6",
}
REFUSAL = re.compile(r"not compiled for the VM \(([^)]*)\)")


def refusal_reason(stderr):
    m = REFUSAL.search(stderr or "")
    return m.group(1) if m else None


def check(argv, cwd, reason, stdin=None, env=None):
    """None when argv is refused exactly as approved under the VM default; else the problem."""
    if reason not in APPROVED:
        return "%s is not an approved refusal" % reason
    with tempfile.TemporaryDirectory(prefix="approved-refusal-") as d:
        stats = os.path.join(d, "stats.jsonl")
        e = {k: v for k, v in (env or os.environ).items() if k != "NIFT_VM"}
        e["NIFT_VM_STATS"] = stats
        p = subprocess.run(argv, cwd=cwd, input=stdin, capture_output=True, text=True, env=e, timeout=60)
        rows = [json.loads(x) for x in open(stats).read().splitlines() if x.strip()] if os.path.exists(stats) else []
    row = rows[-1] if rows else {}
    if (p.returncode != 1 or p.stdout != "" or refusal_reason(p.stderr) != reason
            or "no part of it ran" not in p.stderr or row.get("reason") != reason
            or row.get("vm_executions") != 0 or row.get("legacy_executions") != 0):
        return "not the approved refusal %s (%s) before execution: exit %d stdout %r stderr %r stats %r" % (
            reason, APPROVED[reason], p.returncode, p.stdout, p.stderr[:300], {k: row.get(k) for k in
            ("outcome", "reason", "vm_executions", "legacy_executions")})
    return None
