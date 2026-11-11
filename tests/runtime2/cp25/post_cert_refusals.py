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

--negative-control MODE alters, in memory, every entry, and every entry must then fail:
  root      the recorded root of every scoped unit (the refusal reason changed);
  position  only the representative diagnostic's column, plus one (review L5: position sensitivity on its own);
  both      both at once;
  unlisted-position  one pinned classify-all position per pinned unit is dropped from the record (D-078 #1/#3): the
            sighting is then unlisted, and every pinned entry must fail.

Pinned entries (`unit>root@L:C,L:C` in the scope) cover exactly their recorded classify-all sightings of the reason in
that unit: a new sighting (for example a new push or union-kind refusal) fails the contract, and inventory.py --check
blocks any sighting in a unit outside the scope.

usage: post_cert_refusals.py --nift BIN [--deferrals FILE] [--negative-control root|position|both]
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
           "rt-diff-corpus": "tests/runtime2/rt-diff-corpus",
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
        scope = []
        for x in row["scope"].split(";"):
            if x.strip():
                unit, _, rest = x.strip().partition(">")
                root, _, pins = rest.partition("@")
                scope.append((unit, root, set(pins.split(",")) if pins else None))
        row["scope"] = scope
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
        reasons = {}  # reason -> {"L:C"} of its non-consequential sightings
        if os.path.exists(stats):
            for line in open(stats):
                for it in json.loads(line).get("reasons") or []:
                    if not it.get("consequential"):
                        reasons.setdefault(it["reason"].split(" ")[0], set()).add(
                            "%s:%s" % (it.get("line"), it.get("column")))
        return p.returncode, p.stdout, err, reasons


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--deferrals", default=os.path.join(HERE, "post_cert_deferrals.tsv"))
    ap.add_argument("--negative-control", nargs="?", const="both",
                    choices=["root", "position", "both", "unlisted-position"])
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    rows = load(a.deferrals)
    pinned = {r["reason"] for r in rows if any(p for _, _, p in r["scope"])}
    if a.negative_control:
        import re
        for row in rows:
            if a.negative_control in ("root", "both"):
                row["scope"] = [(u, "negative-control-root", pins) for u, _, pins in row["scope"]]
            if a.negative_control == "unlisted-position":
                # D-078 #1/#3: drop one pinned position per pinned unit; that sighting is then unlisted and must fail.
                row["scope"] = [(u, r, set(sorted(pins)[1:]) if pins else None) for u, r, pins in row["scope"]]
            if a.negative_control in ("position", "both"):
                row["refusal"] = re.sub(r":([0-9]+):([0-9]+): not compiled",
                                        lambda m: ":%s:%d: not compiled" % (m.group(1), int(m.group(2)) + 1),
                                        row["refusal"], count=1)
    failures, not_run, checked = [], [], 0
    with tempfile.TemporaryDirectory(prefix="post-cert-refusals-") as scratch:
        smoke = os.path.join(scratch, "smoke")
        subprocess.run([sys.executable, "-B", os.path.join(REPO, "tools", "rt-diff", "extract_smoke.py"),
                        "--tests", os.path.join(REPO, "tests"), "--out", smoke], check=True, stdout=subprocess.DEVNULL)
        for row in rows:
            reason = row["reason"]
            rep = row["refusal"].split(";", 1)[0].replace("program ", "", 1).strip()
            want = row["refusal"].split("first diagnostic: ", 1)[1].split("; classify-all: ", 1)[0]
            for unit, root, pins in row["scope"]:
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
                # D-078 #1/#3: a pinned entry covers exactly its recorded sightings; any other sighting of the reason in
                # the unit (a new push or union-kind refusal) is unlisted and fails.
                if pins is not None and reasons.get(reason, set()) != pins:
                    failures.append("%s: %s: classify-all sightings %s, pinned %s (an unlisted or missing refusal)"
                                    % (reason, unit, ", ".join(sorted(reasons.get(reason, ()))) or "none",
                                       ", ".join(sorted(pins))))
                checked += 1
    if a.negative_control:
        tested = [r for r in rows if a.negative_control != "unlisted-position" or r["reason"] in pinned]
        missed = [r["reason"] for r in tested if not any(f.startswith(r["reason"] + ": ") for f in failures)]
        if missed or not_run:
            print("FAIL post-cert refusals negative control: not detected for %s; not run %d" % (missed, len(not_run)))
            return 1
        print("PASS post-cert refusals negative control (%s): every one of %d entries fails" % (a.negative_control, len(tested)))
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
