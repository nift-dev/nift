#!/usr/bin/env python3
"""Refusal contract of the D-077 #1 post-certification deferrals (tests/runtime2/cp25/post_cert_deferrals.tsv).

Owner 2026-11-12 04:25 (D-077 #1): a reason may be "DEFERRED — POST-CERTIFICATION IMPLEMENTATION" only with a
reproducible refusal. For every deferred-post-cert entry this re-runs every scoped unit on the given binary:

  - VM default (NIFT_VM unset): exit 1; the first `error:` line names the unit's recorded root,
    `not compiled for the VM (<root>)`; the program did not run ("no part of it ran"); nothing on stdout;
  - NIFT_VM=classify-all: the entry's own reason is listed for the unit (non-consequential), unless the real run
    already refuses with it (the root is the reason itself);
  - the representative program's first diagnostic is the exact line recorded in the entry (position included).

A pass is a refusal-contract pass: it shows that the deferred capability is refused in the approved manner, never
that it works. Units come from the shared corpora as inventory.py names them: vm-corpus/, semantics/, probes/
(docs/evidence/cp410-competitiveness/probes) and smoke/ (heredoc scripts extracted with tools/rt-diff/extract_smoke.py).
A unit whose corpus is not present is NOT RUN, and the suite fails (exit 2), never passes.

--negative-control alters, in memory, every entry's recorded root and representative diagnostic (the position's column
plus one): every entry must then fail, which shows that the contract detects a changed refusal.

usage: post_cert_refusals.py --nift BIN [--deferrals FILE] [--negative-control]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
CORPORA = {"vm-corpus": "tests/runtime2/vm-corpus", "semantics": "tests/runtime2/semantics",
           "probes": "docs/evidence/cp410-competitiveness/probes"}


def load(path):
    rows, header = [], None
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        cells = line.rstrip("\n").split("\t")
        if header is None:
            header = cells
            continue
        row = dict(zip(header, cells))
        row["scope"] = [tuple(x.strip().split(">", 1)) for x in row["scope"].split(";") if x.strip()]
        rows.append(row)
    return rows


def locate(unit, smoke):
    head, rest = unit.split("/", 1)
    if head == "smoke":
        parts = rest.split("/")
        return os.path.join(smoke, parts[0], parts[1]), os.path.join(smoke, rest)
    script = os.path.join(REPO, CORPORA[head], rest)
    return os.path.dirname(script), script


def run(nift, tree, script, scratch, mode):
    with tempfile.TemporaryDirectory(dir=scratch) as td:
        work = os.path.join(td, "w")
        shutil.copytree(tree, work, symlinks=True, ignore=lambda d, n: [x for x in n if x == "rt-diff.only"])
        cwd = os.path.join(work, os.path.relpath(os.path.dirname(script), tree))
        stats = os.path.join(td, "stats.jsonl")
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": td, "LANG": "C"}
        if mode == "classify":
            env.update(NIFT_VM="classify-all", NIFT_VM_STATS=stats)
        p = subprocess.run([nift, os.path.basename(script)], cwd=cwd, env=env, capture_output=True, timeout=60,
                           stdin=subprocess.DEVNULL)
        err = p.stderr.decode(errors="replace")
        for prefix in (os.path.realpath(cwd) + os.sep, cwd + os.sep):
            err = err.replace(prefix, "")
        reasons = set()
        if os.path.exists(stats):
            for line in open(stats):
                for it in json.loads(line).get("reasons") or []:
                    if not it.get("consequential"):
                        reasons.add(it["reason"].split(" ")[0])
        return p.returncode, p.stdout, err, reasons


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--deferrals", default=os.path.join(HERE, "post_cert_deferrals.tsv"))
    ap.add_argument("--negative-control", action="store_true")
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    rows = load(a.deferrals)
    if a.negative_control:
        import re
        for row in rows:
            row["scope"] = [(u, "negative-control-root") for u, _ in row["scope"]]
            row["refusal"] = re.sub(r":([0-9]+):([0-9]+): not compiled",
                                    lambda m: ":%s:%d: not compiled" % (m.group(1), int(m.group(2)) + 1), row["refusal"], count=1)
    failures, not_run, checked = [], [], 0
    with tempfile.TemporaryDirectory(prefix="post-cert-refusals-") as scratch:
        smoke = os.path.join(scratch, "smoke")
        subprocess.run([sys.executable, "-B", os.path.join(REPO, "tools", "rt-diff", "extract_smoke.py"),
                        "--tests", os.path.join(REPO, "tests"), "--out", smoke], check=True, stdout=subprocess.DEVNULL)
        for row in rows:
            reason = row["reason"]
            rep = row["refusal"].split(";", 1)[0].replace("program ", "", 1).strip()
            want = row["refusal"].split("first diagnostic: ", 1)[1].split("; classify-all: ", 1)[0]
            for unit, root in row["scope"]:
                tree, script = locate(unit, smoke)
                if not os.path.isfile(script):
                    not_run.append("%s: %s (corpus not present)" % (reason, unit))
                    continue
                rc, out, err, _ = run(nift, tree, script, scratch, "vm")
                first = next((l for l in err.splitlines() if l.startswith("error:")), "")
                if rc != 1 or out or "not compiled for the VM (%s)" % root not in first or \
                        "no part of it ran" not in err:
                    failures.append("%s: %s: VM default rc %s, stdout %r, first diagnostic %r (recorded root %s)"
                                    % (reason, unit, rc, out[:60], first, root))
                if unit == rep and first != want:
                    failures.append("%s: %s: first diagnostic %r, recorded %r" % (reason, unit, first, want))
                _, _, _, reasons = run(nift, tree, script, scratch, "classify")
                if reason not in reasons and root != reason:  # a direct refusal is shown by the real run itself
                    failures.append("%s: %s: classify-all does not list the reason (lists %s)"
                                    % (reason, unit, ", ".join(sorted(reasons)) or "none"))
                checked += 1
    if a.negative_control:
        missed = [r["reason"] for r in rows if not any(f.startswith(r["reason"] + ": ") for f in failures)]
        if missed or not_run:
            print("FAIL post-cert refusals negative control: not detected for %s; not run %d" % (missed, len(not_run)))
            return 1
        print("PASS post-cert refusals negative control: every one of %d entries fails with an altered root and "
              "diagnostic position" % len(rows))
        return 0
    for f in failures:
        print("FAIL post-cert refusals: %s" % f)
    for n in not_run:
        print("NOT RUN post-cert refusals: %s" % n)
    if failures:
        print("FAIL post-cert refusals: %d failure(s) over %d unit run(s)" % (len(failures), checked))
        return 1
    if not_run:
        print("NOT RUN post-cert refusals: %d unit(s) not run; this is not a pass" % len(not_run))
        return 2
    print("PASS post-cert refusals: %d deferred-post-cert reason(s), %d scoped unit(s) refused as recorded "
          "(a refusal contract, not evidence that the capabilities work)" % (len(rows), checked))
    return 0


if __name__ == "__main__":
    sys.exit(main())
