#!/usr/bin/env python3
"""D-041 module fixtures: specified by hand, not recorded from an engine.

  modules_d041.py --nift ./nift [--legacy-today] [--only NAME]

Each fixture tests/runtime2/modules-d041/<name>/ holds a program or project
in p/ and spec.json: `about`, `legacy` ("agrees", or "differs: ..." naming
the consistency-register entry) and `steps`. A step either edits a file
(`edit`: {path: [old, new]}) or runs the binary in p/ (`run`: argv) and
checks what it states: `stdout`/`stderr` exactly, or `<stream>_contains`/
`<stream>_lacks` substrings, `exit`, `public` (files under public/, exactly) and
`dependencies` (per page, from .nift/public/<page>.info.json). Output lines
starting with "time taken:" are dropped; the work directory reads `$P`.

Without --legacy-today every fixture must pass (the target once D-041 is
implemented in an engine). With --legacy-today the fixtures marked "agrees"
must pass and those marked "differs" must fail, so the specification stays
honest about today's legacy until initialise-once lands (D-041 #2).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "modules-d041")


def norm(data, work):
    text = data.decode("utf-8", "replace").replace(os.path.realpath(work), "$P").replace(work, "$P")
    return "".join(line for line in text.splitlines(True) if not line.startswith("time taken:"))


def check_step(nift, root, work, step):
    problems = []
    if "edit" in step:
        for rel, (old, new) in step["edit"].items():
            path = os.path.join(root, rel)
            text = open(path).read()
            if old not in text:
                return [f"edit: {old!r} not in {rel}"]
            open(path, "w").write(text.replace(old, new, 1))
        return problems
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": work, "LANG": "C"}
    for key in ("NIFT_VM", "NIFT_VM_STATS"):
        if key in os.environ:
            env[key] = os.environ[key]
    p = subprocess.run(["timeout", "60", nift, *step["run"]], cwd=root, env=env, capture_output=True)
    out, err = norm(p.stdout, work), norm(p.stderr, work)
    label = " ".join(step["run"])
    for key, got in (("stdout", out), ("stderr", err)):
        if key in step and got != step[key]:
            problems.append(f"{label}: {key} {got!r}, expected {step[key]!r}")
        if key + "_contains" in step and step[key + "_contains"] not in got:
            problems.append(f"{label}: {key} {got!r} lacks {step[key + '_contains']!r}")
        if key + "_lacks" in step and step[key + "_lacks"] in got:
            problems.append(f"{label}: {key} {got!r} has {step[key + '_lacks']!r}")
    if "exit" in step and p.returncode != step["exit"]:
        problems.append(f"{label}: exit {p.returncode}, expected {step['exit']} (stderr {err!r})")
    for rel, want in step.get("public", {}).items():
        path = os.path.join(root, "public", rel)
        got = open(path).read() if os.path.exists(path) else None
        if got != want:
            problems.append(f"{label}: public/{rel} {got!r}, expected {want!r}")
    for page, want in step.get("dependencies", {}).items():
        path = os.path.join(root, ".nift", "public", page + ".info.json")
        got = json.load(open(path)).get("dependencies") if os.path.exists(path) else None
        if got is None or sorted(got) != sorted(want):
            problems.append(f"{label}: dependencies of {page} {got!r}, expected {want!r}")
    return problems


def run_fixture(nift, name, work):
    dst = os.path.join(work, name)
    shutil.copytree(os.path.join(HERE, name, "p"), os.path.join(dst, "p"))
    spec = json.load(open(os.path.join(HERE, name, "spec.json")))
    for step in spec["steps"]:
        problems = check_step(nift, os.path.join(dst, "p"), dst, step)
        if problems:
            return spec, problems
    return spec, []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--legacy-today", action="store_true")
    ap.add_argument("--only")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    names = sorted(d for d in os.listdir(HERE) if os.path.isdir(os.path.join(HERE, d, "p")))
    if args.only:
        names = [n for n in names if n == args.only]
    failures = 0
    with tempfile.TemporaryDirectory(prefix="mod-d041-") as work:
        for name in names:
            spec, problems = run_fixture(nift, name, work)
            agrees = spec["legacy"] == "agrees"
            if args.legacy_today and not agrees:
                if problems:
                    print(f"ok   {name}: differs today, as recorded ({spec['legacy']})")
                else:
                    failures += 1
                    print(f"FAIL {name}: recorded as differing but legacy now passes; update spec.json")
                continue
            if problems:
                failures += 1
                print(f"FAIL {name}:\n  " + "\n  ".join(problems))
            else:
                print(f"ok   {name}")
    mode = "legacy-today" if args.legacy_today else "D-041"
    print(f"{'PASS' if not failures else 'FAIL'} modules-d041 ({mode}): {len(names)} fixtures, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
