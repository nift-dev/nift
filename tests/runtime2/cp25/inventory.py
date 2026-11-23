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

`deferred-post-cert` (owner 2026-11-12 04:25, D-077 #1: "DEFERRED — POST-CERTIFICATION IMPLEMENTATION"): a named
capability whose implementation D-076 (or an earlier explicit deferral) schedules after the first certification
baseline. It is not `deferred-named` (a deferral from the initial release), and it is not accepted by the gates that
accept an approved or deferred-named refusal (approved_refusal.py and the parity gates): a refusal-contract pass is
not evidence that the capability works. Each such row needs an entry in post_cert_deferrals.tsv (--deferrals):
the capability, the decision references, the refusal evidence, and the scope: the exact units (`unit>root`, the
unit's real-run refusal reason) in which the reason is deferred. --check:
  - G1 fails (exit 1) when a deferred-post-cert row has no complete entry, or an entry names a row that is not
    deferred-post-cert;
  - blocks (exit 3) when a deferred-post-cert reason is seen as a refusal in a unit outside its scope, or a scoped
    unit's real-run reasons no longer include its recorded root (scope drift): a deferral never covers a new
    program or a changed cause without review.
When nothing blocks, --check says "Inventory complete, with named deferred capabilities" and reports the four
classes separately (supported, approved difference, named deferred, unresolved). It never reports that all
supported functionality passes: a refusal inventory cannot show that, and a deferred capability does not execute.
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "tools" / "rt-vm"))
STATUSES = ("approved", "deferred-named", "deferred-post-cert", "implement", "implemented-awaiting-review",
            "implemented-accepted", "PENDING-OWNER", "retired")
DEFERRAL_FIELDS = ("reason", "capability", "decision", "scope", "refusal")
COMPLETE = "Inventory complete, with named deferred capabilities"
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


def load_deferrals(path):
    """post_cert_deferrals.tsv (D-077 #1): reason -> {capability, decision, scope: {unit: root}, refusal}. A missing
    file is empty (then every deferred-post-cert row fails G1)."""
    rows = {}
    if not path or not os.path.exists(path):
        return rows
    header = None
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        cells = line.rstrip("\n").split("\t")
        if header is None:
            header = cells
            continue
        row = dict(zip(header, cells))
        scope = {}
        for item in (row.get("scope") or "").split(";"):
            unit, _, root = item.strip().partition(">")
            if unit.strip():
                scope[unit.strip()] = root.split("@")[0].strip()  # `@L:C,...`: pinned sightings (post_cert_refusals.py)
        row["scope"] = scope
        rows[row.get("reason", "")] = row
    return rows


def unit_of(uid):
    """classify-all ids are `path|script`, execute entries `path`: compare paths."""
    return (uid or "").split("|")[0]


# D-158: these historical method sightings follow a reproduced upstream
# refusal. Retain the original classifier output and separate the sightings
# in the report. A changed root or a real run refusing the method still blocks.
D158_UPSTREAM = {
    "probes/sort-aggregate-2000.f|sort-aggregate-2000.f": ("callable:inject", {"method:sort_by": {2}}),
    "probes/sort-multi-2000.f|sort-multi-2000.f": ("callable:inject", {"method:sort_by": {2}}),
    "rt-diff-corpus/workspaces/script-outside|main.f": ("native-unqualified", {"method:stringify": {3}}),
    "smoke/v44_cp2_variadic_functions_smoke/001-ok/ok.f|ok.f": ("fn-flags", {"method:to_string": {5}}),
    "smoke/v44_executable_script_smoke/003-r/r.f|r.f": ("callable:run", {"method:to_string": {2}}),
    "smoke/v44_scalar_conversion_smoke/002-a/a.f|a.f": (
        "opaque:legacy-runtime-error", {"method:to_string": {4, 5, 6, 7, 8, 10, 11, 12, 14, 16, 17, 26}}
    ),
    "smoke/v44_scalar_conversion_smoke/003-o/o.f|o.f": (
        "builtin-receiver:to_string", {"method:to_string": {1}}
    ),
    "smoke/v46_b4_cp1_characterization/001-names/names.f|names.f": (
        "opaque:legacy-runtime-error", {"method:catch": {21}, "method:error": {20}, "method:throw": {22}}
    ),
}


# Replayed undefined lookups and a write to an undeclared binding. Retain the
# refusal and report a new site or unit instead of approving the whole family.
D158_UNDEFINED = {
    "rt-diff-corpus/eval/values.eval|<eval>": {1},
    "semantics/assign_undeclared.f|assign_undeclared.f": {2},
    "smoke/v46_b4_cp1_characterization/007-bad-import/bad-import.f|bad-import.f": {1},
}

def report(args):
    d = args.dir
    cls = load_classification(args.classification)
    deferrals = load_deferrals(args.deferrals)
    full = collections.defaultdict(set)      # reason -> units (classify-all, non-consequential)
    conseq = collections.defaultdict(set)
    deferred = collections.defaultdict(set)  # slot 3: reason -> units with a deferred site of it (5.4)
    refused = set()  # reasons seen as a refusal (not only as a deferred site): REVIEW-C136-SLOT3 F-A
    refused_units = collections.defaultdict(set)  # reason -> units refusing with it (not deferred sites): D-077 #1
    refusal_lines = collections.defaultdict(set)
    for line in open(os.path.join(d, "classify-all.jsonl")):
        r = json.loads(line)
        for it in r.get("reasons") or []:
            (conseq if it.get("consequential") else full)[reason_key(it["reason"])].add(r["id"])
            if (it.get("reason") or "").startswith("deferred:") and not it.get("consequential"):
                deferred[reason_key(it["reason"])].add(r["id"])
            elif not it.get("consequential"):
                refused.add(reason_key(it["reason"]))
                refusal_lines[r["id"], reason_key(it["reason"])].add(it.get("line"))
                refused_units[reason_key(it["reason"])].add(unit_of(r["id"]))
    fallback = collections.defaultdict(set)  # reason -> entries leaving the VM (execute)
    unit_reasons = collections.defaultdict(set)  # execute entry -> its non-VM reasons (scope roots, D-077 #1)
    entries = fb_entries = 0
    for line in open(os.path.join(d, "execute.jsonl")):
        r = json.loads(line)
        entries += 1
        left = False
        for e in r["executions"]:
            if e["outcome"] not in ("vm", "vm-error"):
                left = True
                fallback[reason_key(e["reason"])].add(r["entry"])
                refused_units[reason_key(e["reason"])].add(unit_of(r["entry"]))
                unit_reasons[unit_of(r["entry"])].add(reason_key(e["reason"]))
        fb_entries += left
    upstream = []
    for uid, (root, methods) in D158_UPSTREAM.items():
        unit = unit_of(uid)
        if root not in unit_reasons[unit]:
            continue
        for method, reviewed_lines in methods.items():
            lines = refusal_lines.get((uid, method))
            if (not lines or not lines <= reviewed_lines or method in unit_reasons[unit]
                    or uid in deferred[method]):
                continue
            upstream.append((uid, root, method))
            full[method].discard(uid)
            if not any(unit_of(other) == unit for other in full[method]):
                refused_units[method].discard(unit)
            if not full[method]:
                del full[method]
            if not refused_units[method]:
                refused.discard(method)
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
    post_cert = sorted(r for r in seen if (row_for(r) or {}).get("status") == "deferred-post-cert")
    if post_cert:  # D-077 #1: named deferred capabilities are refusals, never passing functionality
        out += ["", "## Named deferred capabilities (D-077 #1, DEFERRED — POST-CERTIFICATION IMPLEMENTATION)", "",
                "These reasons are refused. A refusal-contract pass is not evidence that the capability works.", ""]
        for r in post_cert:
            e = deferrals.get(r) or {}
            out.append("- `%s`: %s (%s); scope %d unit(s)" % (r, e.get("capability", "?"), e.get("decision", "?"),
                                                            len(e.get("scope") or {})))
    if upstream:
        out += ["", "## D-158 method sightings after a verified upstream refusal", ""]
        out += ["- `%s` in `%s` follows `%s`." % (method, unit, root)
                for unit, root, method in upstream]
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
    # D-077 #1: every deferred-post-cert row has a complete deferral entry (capability, decision references, scope with
    # a recorded root per unit, refusal evidence), and every entry belongs to a deferred-post-cert row.
    for reason, row in sorted(cls.items()):
        if row.get("status") == "deferred-post-cert":
            entry = deferrals.get(reason) or {}
            missing = [f for f in DEFERRAL_FIELDS if not entry.get(f)]
            if entry.get("decision") and not re.search(r"\bD-0[0-9][0-9]\b", entry["decision"]):
                missing.append("decision reference (D-0nn)")
            if any(not root for root in (entry.get("scope") or {}).values()):
                missing.append("a root for every scope unit")
            if missing:
                p = "deferred-post-cert row %s has no complete deferral entry (missing %s)" % (reason, ", ".join(missing))
                problems.append(p)
                print("INVENTORY:", p, file=sys.stderr)
    for reason in sorted(deferrals):
        if (cls.get(reason) or {}).get("status") != "deferred-post-cert":
            p = "deferral entry %s names a row whose status is %r, not deferred-post-cert" % (
                reason, (cls.get(reason) or {}).get("status"))
            problems.append(p)
            print("INVENTORY:", p, file=sys.stderr)
    awaiting = sorted(r for r, row in cls.items() if row.get("status") == "implemented-awaiting-review")
    by_status = collections.defaultdict(list)
    for reason in sorted(seen):
        by_status[(row_for(reason) or {}).get("status", "?")].append(reason)
    owner = by_status.get("PENDING-OWNER", [])
    defined_scope = {unit_of(uid) for uid in D158_UNDEFINED}
    undefined_drift = sorted(
        {uid for uid in full.get("unresolved-name", ())
         if not refusal_lines.get((uid, "unresolved-name"), set()) <= D158_UNDEFINED.get(uid, set())}
        | (refused_units.get("unresolved-name", set()) - defined_scope)
    )
    if args.check:
        print("INVENTORY: G1 %s: %d reason(s) seen, %s" % (
            "FAIL" if problems else "PASS", len(seen),
            ", ".join("%s %d" % (k, len(by_status.get(k, []))) for k in STATUSES)), file=sys.stderr)
        for status in ("deferred-named", "deferred-post-cert", "implement"):
            print("INVENTORY: %s: %s" % (status, ", ".join(by_status.get(status, [])) or "none"), file=sys.stderr)
        if owner:
            print("INVENTORY: BLOCKED: %d PENDING-OWNER reason(s), not adjudicated: %s" % (len(owner), ", ".join(owner)),
                  file=sys.stderr)
    if args.check and problems:
        return 1
    blocked = False
    if args.check and undefined_drift:
        print("INVENTORY: BLOCKED: unreviewed unresolved-name sites: " +
              "; ".join(undefined_drift), file=sys.stderr)
        blocked = True
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
    # D-077 #1: a deferred-post-cert reason is deferred only in its recorded units, and only while each unit's real
    # run still refuses with the recorded root. A new unit or a changed cause blocks until it is reviewed.
    outside, drift = [], []
    for reason in by_status.get("deferred-post-cert", []):
        scope = (deferrals.get(reason) or {}).get("scope") or {}
        for u in sorted(refused_units.get(reason, ())):
            if u not in scope:
                outside.append("%s (%s)" % (reason, u))
            elif scope.get(u) not in unit_reasons.get(u, ()):
                drift.append("%s (%s: real run %s, recorded %s)" % (
                    reason, u, ", ".join(sorted(unit_reasons.get(u, ()))) or "VM only", scope.get(u)))
    if args.check and outside:
        print("INVENTORY: BLOCKED: deferred-post-cert reason(s) seen outside their recorded scope: %s" %
              "; ".join(outside), file=sys.stderr)
        blocked = True
    if args.check and drift:
        print("INVENTORY: BLOCKED: deferred-post-cert scope drift (the recorded root no longer refuses): %s" %
              "; ".join(drift), file=sys.stderr)
        blocked = True
    if blocked:
        return 3
    if args.check and owner:
        return 3
    if args.check:
        # D-077 #1 wording: never "all supported functionality passes". The four classes stay distinct.
        print("INVENTORY: %s. Supported: %d of %d execute entries ran entirely on the VM (their correctness is "
              "established by the differential and gate suites, not by this inventory). Approved difference: %d "
              "reason(s). Named deferred (refused, not executed): deferred-named %d, deferred-post-cert %d. Approved "
              "for implementation, not implemented (refused): %d. Unresolved defect or decision (PENDING-OWNER): 0." % (
                  COMPLETE, entries - fb_entries, entries, len(by_status.get("approved", [])),
                  len(by_status.get("deferred-named", [])), len(by_status.get("deferred-post-cert", [])),
                  len(by_status.get("implement", []))), file=sys.stderr)
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
    r.add_argument("--deferrals", default=str(HERE / "post_cert_deferrals.tsv"))
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
