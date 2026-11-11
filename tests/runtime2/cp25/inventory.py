#!/usr/bin/env python3
"""CP25-R refusal inventory (gate G1, docs/runtime-2/checkpoints/cp25r-scope.md).

  inventory.py collect --nift ./nift --out DIR [--seed 1] [--labs DIR]
  inventory.py report DIR [--classification FILE] [--markdown OUT] [--check]

collect runs the release script corpora twice:
  classify-all  NIFT_VM=classify-all over the shared corpora exactly as the
                CP12b inventory sees them (tools/rt-vm/cp12b_inventory.py:
                vm-corpus, semantics, rt-diff-corpus, conformance, probes and
                the scripts extracted from smoke-test heredocs; rt-diff's
                generated programs are excluded). Nothing executes; every
                refusal reason of every unit is listed (src/rt/refusal_inventory.h).
  execute       real runs (NIFT_VM=1, tools/rt-vm/nift-vm) of the same
                shared corpora and smoke scripts through rt-diff, which sets
                up each entry exactly as test-rt-vm does (--b-vm-stats):
                which units actually leave the VM, and why. This is the
                authoritative fallback list. classify-all compiles without
                the run path's switches (CP23 parallel default, linked
                modules), so it can list spurious thread/coroutine reasons and
                miss coro-module; those rows are classified H.
report joins both with the committed classification
(tests/runtime2/cp25/classification.tsv): reason, class (M A I T P H X),
legacy behaviour, disposition, G2 approval, evidence. --check fails when a
reason seen on the corpora has no classification row (G1), or a row names an
unknown class or status. The `status` column (approved, deferred-named,
implement, PENDING-OWNER; owner 2026-11-10 13:26; and retired for a resolved or
no longer emitted reason, which counts as none of them) says where each reason stands
for G2: --check reports every status, and exits 3 when a seen reason is still
PENDING-OWNER (a release blocker), after G1 has passed.

The owner's five implementation statuses (2026-11-11 11:07, decisions.md D-069 #4) map onto this
vocabulary: approved-not-implemented = `implement`; implemented-awaiting-review =
`implemented-awaiting-review`; implemented-accepted = `implemented-accepted`; approved deferral =
`deferred-named`; pending owner = `PENDING-OWNER`. A row is never marked implemented because it is
scheduled. --check treats them so:
  implemented-awaiting-review  a release blocker whether or not the reason is seen (exit 3): the
                               implementation is not accepted yet;
  implemented-accepted         the capability is implemented and accepted, so the refusal must not be
                               emitted any more: seen again, it blocks (exit 3), like a retired reason.
Every row of the classification must carry one of STATUSES (G1, exit 1), seen or not.
"""
import argparse
import collections
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "tools" / "rt-vm"))
STATUSES = ("approved", "deferred-named", "implement", "implemented-awaiting-review", "implemented-accepted",
            "PENDING-OWNER", "retired")
# Family grouping (owner 2026-11-11 02:53, D-067 #10): only these four reason families have a family row
# `<prefix>:*` that classifies an unlisted member. Every other reason needs its own exact row; an unlisted
# `type:<op>` (or any other) reason fails G1 (slot 1, D-068 #11: `type:*` is no longer a family row).
FAMILIES = ("builtin-arity", "builtin-receiver", "method", "callable")
CLASSES = {"M": "missing VM capability", "A": "compiler/admission limitation",
           "I": "intentional semantic refusal", "T": "deferred CP24", "P": "portability",
           "H": "test harness/fixture", "X": "outside release contract"}


def reason_key(reason):
    """Group reasons as the classification does: the reason text up to the
    first space (`callable:cmd`, `type:print`, `static-error:arity`). A deferred site (slot 3,
    dead-code-admission.md 5.4: `deferred:<reason>`, recorded by classify-all for a site compiled to a
    raise) maps to its reason's row, so the row's status applies to it as to a refusal."""
    key = (reason or "").split(" ")[0]
    return key[len("deferred:"):] if key.startswith("deferred:") else key


def collect_classify_all(nift, out, seed):
    import cp12b_inventory
    with tempfile.TemporaryDirectory(prefix="cp25-inv-") as work:
        old = os.getcwd()
        try:
            os.chdir(REPO)
            n = cp12b_inventory.collect_shared(nift, out, seed, work)
        finally:
            os.chdir(old)
    rows = [json.loads(l) for l in open(os.path.join(out, "shared.jsonl"))]
    kept = [r for r in rows if not r["id"].startswith("gen")]
    with open(os.path.join(out, "classify-all.jsonl"), "w") as f:
        for r in kept:
            f.write(json.dumps(r, sort_keys=True) + "\n")
    os.remove(os.path.join(out, "shared.jsonl"))
    return n, len(kept)


def collect_execute_rt_diff(nift, out, seed):
    import cp12b_inventory
    with tempfile.TemporaryDirectory(prefix="cp25-exec-") as work:
        report = os.path.join(work, "report.json")
        # rt-diff's minimal environment drops NIFT_VM_BIN, so tools/rt-vm/nift-vm always ran the
        # checkout's ./nift (review of 4ec1aa80, F1): a wrapper names the binary itself.
        vm = os.path.join(work, "nift-vm")
        with open(vm, "w") as f:
            f.write("#!/bin/sh\nNIFT_VM=1\nexport NIFT_VM\nexec '%s' \"$@\"\n" % nift.replace("'", "'\\''"))
        os.chmod(vm, 0o755)
        cmd = [sys.executable, "tools/rt-diff/rt-diff", "--a", vm, "--b", vm,
               "--no-trace", "--no-interleave", "--jobs", "1", "--extract-smoke", "tests", "--gen", "0",
               "--seed", str(seed), "--quiet", "--b-vm-stats", "--report", report, "--timeout", "60"]
        for c in cp12b_inventory.SHARED:
            cmd += ["--corpus", c]
        env = dict(os.environ, NIFT_VM_BIN=nift)
        # B18: rt-diff exits 1 when it finds differences (expected here: VM against itself with
        # stats); anything else (2: usage, a crash) is a harness failure, not an inventory.
        rd = subprocess.run(cmd, cwd=REPO, env=env, stdout=subprocess.DEVNULL)
        if rd.returncode not in (0, 1):
            sys.exit("cp25 inventory: rt-diff failed (exit %d)" % rd.returncode)
        rows = []
        for r in json.load(open(report))["results"]:
            execs = [{"outcome": st.get("outcome"), "reason": st.get("reason", ""),
                      "path": os.path.basename(st.get("path", ""))} for st in r.get("vm_stats") or []]
            rows.append({"corpus": r["id"].split("/")[0], "entry": r["id"], "rc": None, "executions": execs})
    return rows


def run_entry(nift, script, base_label):
    with tempfile.TemporaryDirectory(prefix="cp25-exec-") as td:
        work = Path(td) / "w"
        shutil.copytree(script.parent, work, symlinks=True)
        stats = Path(td) / "stats.jsonl"
        env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
        extra = script.with_suffix(".env")
        if extra.exists():
            for line in extra.read_text().splitlines():
                if "=" in line:
                    k, v = line.split("=", 1)
                    env[k] = v
        env.update(NIFT_VM="1", NIFT_VM_STATS=str(stats), HOME=td, LANG="C")
        args = script.with_suffix(".args")
        argv = [nift, script.name] + (args.read_text().splitlines() if args.exists() else [])
        stdin = script.with_suffix(".stdin")
        try:
            p = subprocess.run(argv, cwd=work, env=env, capture_output=True, timeout=60,
                               input=stdin.read_bytes() if stdin.exists() else b"")
            rc = p.returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
        execs = []
        if stats.exists():
            for line in stats.read_text().splitlines():
                try:
                    s = json.loads(line)
                except ValueError:
                    continue
                execs.append({"outcome": s.get("outcome"), "reason": s.get("reason", ""),
                              "path": os.path.basename(s.get("path", ""))})
        return {"corpus": base_label, "entry": str(script), "rc": rc, "executions": execs}


def collect_execute(nift, out, labs, seed):
    rows = collect_execute_rt_diff(nift, out, seed)
    corpora = [(Path(labs), "labs")] if labs else []
    for base, label in corpora:
        for script in sorted(base.rglob("*.f")):
            rel = script.relative_to(base)
            if any(part.startswith(".") for part in rel.parts):
                continue
            r = run_entry(nift, script, label)
            r["entry"] = str(rel)
            rows.append(r)
    with open(os.path.join(out, "execute.jsonl"), "w") as f:
        for r in rows:
            f.write(json.dumps(r, sort_keys=True) + "\n")
    return len(rows)


def load_classification(path):
    rows = {}
    with open(path) as f:
        header = None
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


def report(args):
    d = args.dir
    cls = load_classification(args.classification)
    full = collections.defaultdict(set)      # reason -> units (classify-all, non-consequential)
    conseq = collections.defaultdict(set)
    deferred = collections.defaultdict(set)  # slot 3: reason -> units with a deferred site of it (5.4)
    refused = set()  # reasons seen as a refusal (not only as a deferred site): REVIEW-C136-SLOT3 F-A
    for line in open(os.path.join(d, "classify-all.jsonl")):
        r = json.loads(line)
        for it in r.get("reasons") or []:
            (conseq if it.get("consequential") else full)[reason_key(it["reason"])].add(r["id"])
            if (it.get("reason") or "").startswith("deferred:") and not it.get("consequential"):
                deferred[reason_key(it["reason"])].add(r["id"])
            elif not it.get("consequential"):
                refused.add(reason_key(it["reason"]))
    fallback = collections.defaultdict(set)  # reason -> entries leaving the VM (execute)
    entries = fb_entries = 0
    for line in open(os.path.join(d, "execute.jsonl")):
        r = json.loads(line)
        entries += 1
        left = False
        for e in r["executions"]:
            if e["outcome"] not in ("vm", "vm-error"):
                left = True
                fallback[reason_key(e["reason"])].add(r["entry"])
        fb_entries += left
    seen = set(full) | set(fallback)
    refused |= set(fallback)

    def row_for(reason):
        """An exact row, else the family row `<prefix>:*` of an approved family (FAMILIES)."""
        if reason in cls:
            return cls[reason]
        prefix = reason.split(":")[0]
        return cls.get(prefix + ":*") if ":" in reason and prefix in FAMILIES else None

    problems = []
    for reason in sorted(seen):
        row = row_for(reason)
        if row is None:
            problems.append("unclassified reason: " + reason)
        elif row.get("class") not in CLASSES:
            problems.append("unknown class %r for %s" % (row.get("class"), reason))
    by_class = collections.Counter()
    out = ["# CP25-R refusal inventory", "",
           "Generated by `tests/runtime2/cp25/inventory.py` from the candidate binary; do not edit.", "",
           "Execute mode: %d entries, %d with any non-VM execution." % (entries, fb_entries), "",
           "| Reason | Class | Fallback entries | Units refused (classify-all) | Legacy behaviour | Disposition | G2 approval | Evidence |",
           "|---|---|---:|---:|---|---|---|---|"]
    for reason in sorted(seen, key=lambda r: (-len(fallback.get(r, ())), -len(full.get(r, ())), r)):
        row = row_for(reason) or {}
        by_class[row.get("class", "?")] += len(fallback.get(reason, ()))
        out.append("| `%s` | %s | %d | %d | %s | %s | %s | %s |" % (
            reason, row.get("class", "**UNCLASSIFIED**"), len(fallback.get(reason, ())), len(full.get(reason, ())),
            row.get("legacy", ""), row.get("disposition", ""), row.get("approval", ""), row.get("evidence", "")))
    out += ["", "Fallback entries by class: " + ", ".join("%s %d" % kv for kv in sorted(by_class.items())), "",
            "## Fallback entries by reason", ""]
    for reason in sorted(fallback, key=lambda r: (-len(fallback[r]), r)):
        out.append("- `%s`: %s" % (reason, ", ".join(sorted(fallback[reason]))))
    if deferred:  # never fallback entries: the unit runs on the VM and raises only if the site is reached
        out += ["", "## Deferred sites (slot 3; the row's status applies)", ""]
        out += ["- `%s`: %d unit(s): %s" % (r, len(u), ", ".join(sorted(u))) for r, u in sorted(deferred.items())]
    if conseq:
        out += ["", "Consequential classify-all reasons (may be spurious, not classified): " +
                ", ".join("`%s` %d" % (r, len(u)) for r, u in sorted(conseq.items()) if r not in full)]
    text = "\n".join(out) + "\n"
    if args.markdown:
        open(args.markdown, "w").write(text)
    else:
        sys.stdout.write(text)
    for p in problems:
        print("INVENTORY:", p, file=sys.stderr)
    # Owner 2026-11-10 13:26, item 6 (combined-b3-b5.md section 81): every row has a status, and a row
    # never implies approval by itself. G1 (every seen reason has a row with a valid status) and the
    # release state are reported separately: a PENDING-OWNER reason is a release blocker, so --check
    # fails on it (exit 3) even when G1 passes.
    for reason in sorted(seen):
        row = row_for(reason)
        if row is not None and row.get("status") not in STATUSES:
            problems.append("invalid status %r for %s (one of %s)" % (row.get("status"), reason, ", ".join(STATUSES)))
    for reason, row in sorted(cls.items()):  # D-069 #4: every row, seen or not, has a valid status
        if reason not in seen and row.get("status") not in STATUSES:
            p = "invalid status %r for row %s (one of %s)" % (row.get("status"), reason, ", ".join(STATUSES))
            problems.append(p)
            print("INVENTORY:", p, file=sys.stderr)
    awaiting = sorted(r for r, row in cls.items() if row.get("status") == "implemented-awaiting-review")
    by_status = collections.defaultdict(list)
    for reason in sorted(seen):
        by_status[(row_for(reason) or {}).get("status", "?")].append(reason)
    owner = by_status.get("PENDING-OWNER", [])
    if args.check:
        print("INVENTORY: G1 %s: %d reason(s) seen, %s" % (
            "FAIL" if problems else "PASS", len(seen),
            ", ".join("%s %d" % (k, len(by_status.get(k, []))) for k in STATUSES)), file=sys.stderr)
        for status in ("deferred-named", "implement"):
            print("INVENTORY: %s: %s" % (status, ", ".join(by_status.get(status, [])) or "none"), file=sys.stderr)
        if owner:
            print("INVENTORY: BLOCKED: %d PENDING-OWNER reason(s), not adjudicated: %s" % (len(owner), ", ".join(owner)),
                  file=sys.stderr)
    if args.check and problems:
        return 1
    blocked = False
    # REVIEW-C136-SLOT3 F-A: a deferred site is not a refusal (the unit runs on the VM), so a retired or
    # implemented-accepted reason is "seen again" only as a refusal. Deferred sites still need a row (G1), keep their
    # row's status for PENDING-OWNER, and are listed in the report's deferred-site section.
    for status in ("retired", "implemented-accepted"):
        by_status[status] = [r for r in by_status.get(status, []) if r in refused]
    if args.check and by_status.get("retired"):
        # A retired reason (resolved, not emitted any more) that is seen again is not a pass either.
        print("INVENTORY: BLOCKED: retired reason(s) seen again: %s" % ", ".join(by_status["retired"]), file=sys.stderr)
        blocked = True
    if args.check and by_status.get("implemented-accepted"):
        print("INVENTORY: BLOCKED: implemented-accepted reason(s) seen again: %s" %
              ", ".join(by_status["implemented-accepted"]), file=sys.stderr)
        blocked = True
    if args.check and awaiting:
        print("INVENTORY: BLOCKED: %d row(s) implemented, awaiting review: %s" % (len(awaiting), ", ".join(awaiting)),
              file=sys.stderr)
        blocked = True
    if blocked:
        return 3
    if args.check and owner:
        return 3
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("collect")
    c.add_argument("--nift", required=True)
    c.add_argument("--out", required=True)
    c.add_argument("--seed", type=int, default=1)
    c.add_argument("--labs")
    r = sub.add_parser("report")
    r.add_argument("dir")
    r.add_argument("--classification", default=str(HERE / "classification.tsv"))
    r.add_argument("--markdown")
    r.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if a.cmd == "collect":
        os.makedirs(a.out, exist_ok=True)
        nift = os.path.abspath(a.nift)
        n, kept = collect_classify_all(nift, a.out, a.seed)
        m = collect_execute(nift, a.out, a.labs, a.seed)
        print("classify-all: %d units (%d excluding generated); execute: %d entries" % (n, kept, m))
        return 0
    return report(a)


if __name__ == "__main__":
    sys.exit(main())
