#!/usr/bin/env python3
"""B5 (docs/runtime-2/checkpoints/b5-refsem.md): the reference-semantics fixtures.

  run.py --nift NIFT [--dirs D ...] [--update]

Every fixture under tests/runtime2/b5/<dir>/ is a script `X.f` (run as `nift X.f`) or a
session `X.in` (fed to `nift -i`), with its golden `X.out`: the expected stdout, then a line
`-- exit N`, then the expected error lines (stderr lines that start with `error:`, with the
fixture's path removed). Goldens are hand-derived from the Phase B semantics table (plan
section 3; the derivation is the fixture's comment or the record's table) and checked here
on the production build: arrays and objects are shared references.

Every run also fails on an internal failure (exit 70, a signal, `compiler defect`,
`internal failure`, an internal tag) and on a nonzero dispatch-defect counter. Programs run in
a temporary copy of their directory; the repository is never written (except `--update`,
which writes the goldens of the named directories for review).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
INTERNAL = re.compile(r"compiler defect|internal failure|unexpected operands|\x1fnift:|\\u001fnift:|cell reached")


def run(nift, path):
    src = os.path.dirname(path)
    work = tempfile.mkdtemp(prefix="b5-run-")
    try:
        tree = os.path.join(work, "t")
        shutil.copytree(src, tree)
        stats = os.path.join(work, "s.jsonl")
        env = {"PATH": "/usr/bin:/bin", "HOME": work, "TMPDIR": work, "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
               "NO_COLOR": "1", "NIFT_VM": "require", "NIFT_VM_STATS": stats}
        name = os.path.basename(path)
        if name.endswith(".in"):
            cmd, stdin = ["timeout", "60", nift, "-i"], open(path, "rb").read()
        else:
            cmd, stdin = ["timeout", "60", nift, name], b""
        p = subprocess.run(cmd, cwd=tree, env=env, input=stdin, capture_output=True)
        defects = 0
        if os.path.exists(stats):
            for line in open(stats):
                try:
                    defects += int(json.loads(line).get("dispatch_defects", 0))
                except ValueError:
                    pass
        out = p.stdout.decode("utf-8", "replace")
        err = p.stderr.decode("utf-8", "replace").replace(tree + "/", "").replace(tree, ".")
        return out, err, p.returncode, defects
    finally:
        shutil.rmtree(work, ignore_errors=True)


def render(out, err, rc):
    errors = [l for l in err.splitlines() if l.startswith("error:")]
    return out + ("" if out.endswith("\n") or not out else "\n") + "-- exit %d\n" % rc + "".join(e + "\n" for e in errors)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--dirs", nargs="+", default=None)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    dirs = args.dirs or sorted(d for d in os.listdir(HERE) if os.path.isdir(os.path.join(HERE, d)))
    failures, count = [], 0
    for d in dirs:
        base = os.path.join(HERE, d)
        for name in sorted(os.listdir(base)):
            if not (name.endswith(".f") or name.endswith(".in")) or name.startswith("_"):
                continue
            path = os.path.join(base, name)
            if name.endswith(".f") and os.path.exists(os.path.join(base, ".modules")):
                continue  # module helpers of a directory fixture
            count += 1
            out, err, rc, defects = run(args.nift, path)
            label = "%s/%s" % (d, name)
            if rc == 70 or rc < 0 or rc >= 128 or INTERNAL.search(out + err) or defects:
                failures.append("%s: internal failure (exit %d, defects %d): %r" % (label, rc, defects, err[-300:]))
                continue
            got = render(out, err, rc)
            golden = path.rsplit(".", 1)[0] + ".out"
            if args.update:
                open(golden, "w").write(got)
                continue
            if not os.path.exists(golden):
                failures.append("%s: no golden %s" % (label, os.path.basename(golden)))
                continue
            want = open(golden).read()
            if got != want:
                failures.append("%s:\n--- want\n%s--- got\n%s" % (label, want, got))
    for f in failures:
        print("FAIL " + f)
    if failures:
        print("FAIL b5 fixtures: %d of %d" % (len(failures), count))
        return 1
    print("PASS b5 fixtures: %d programs (%s)" % (count, ", ".join(dirs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
