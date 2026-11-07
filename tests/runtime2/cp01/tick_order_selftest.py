#!/usr/bin/env python3
"""Regression test for the CP01 flakiness (2026-11-08): it must catch the
original defect deterministically.

The defect: an edit followed at once by a build could share the filesystem's
coarse timestamp tick with the page-info that build writes (equal mtimes with
multigrain timestamps), so the v4.12 modified-mode rule (rebuild when a
dependency is not older than its page-info) rebuilt the page once more on the
next build. Seen as S3b/S4a/S4e extra rebuilds, `S8e no-op ... rebuilt [N, 0,
0, 0, 0]` and settle `after-edit-all:[ 20 1 0 ... ]`, on every binary tried
(v4.12.0, 17f7964c, fc987ec1, the candidate), at random.

The test forces the race: CP01_WORST_CASE_TICK=1 stamps every page-info a build
writes with the earliest time it legally could carry (the fastest possible
build). With the old, unsequenced edits that is the edit's own mtime, and the
original failures must appear; with sequenced edits (ticks.py) it is the clock
after the edit, and nothing may be rebuilt twice.

usage: tick_order_selftest.py NIFT
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True  # no __pycache__ in the tree
sys.path.insert(0, HERE)
import ticks  # noqa: E402

nift = sys.argv[1]
fails = []


def ordinary(edit):
    env = dict(os.environ, ONLY="basic", CYCLES="0", CP01_EDIT=edit, CP01_WORST_CASE_TICK="1")
    p = subprocess.run([sys.executable, os.path.join(HERE, "ordinary.py"), f"ours={nift}"],
                       capture_output=True, text=True, env=env)
    return p.returncode, p.stdout + p.stderr


# 1. the ordinary scenarios, forced to the fastest build
rc, out = ordinary("unsequenced")
want = ["S4a create unreferenced file: rebuilt", "S4e edit removed dependency (no rebuild): rebuilt",
        "S8e no-op after mode switches rebuilt"]
missing = [w for w in want if w not in out]
if rc == 0 or missing:
    fails.append(f"forced race, unsequenced edits: the original extra rebuilds did not appear (rc={rc}, missing {missing})")
rc, out = ordinary("sequenced")
if rc != 0 or "FAILS (0):" not in out:
    fails.append("forced race, sequenced edits: " + "; ".join(l.strip() for l in out.splitlines() if "FAIL" in l)[:400])


# 2. settle: edit all 20 pages, then back-to-back builds
def count(root):
    p = subprocess.run([nift, "build"], cwd=root, capture_output=True, text=True)
    if p.returncode != 0:
        fails.append(f"settle build failed rc={p.returncode}: {p.stdout.strip()[:200]}")
    out = p.stdout
    names = [l for l in out.splitlines() if l.startswith("built ")]
    m = re.search(r"(\d+) files? (?:re)?built", out)
    return len(names) or (int(m.group(1)) if m else 0)


def infos(root):
    out = {}
    for dp, _, fs in os.walk(os.path.join(root, ".nift")):
        for f in fs:
            if f.endswith(".info.json"):
                out[os.path.join(dp, f)] = os.stat(os.path.join(dp, f)).st_mtime_ns
    return out


def settle(edit):
    root = tempfile.mkdtemp(prefix="cp01-tick-")
    try:
        for d in (".nift", "content", "templates", "public"):
            os.makedirs(os.path.join(root, d))
        import json
        json.dump({"config": {"content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
                              "output-ext": ".html", "default-template": "templates/template.html", "build-threads": 1,
                              "incremental-mode": "modified", "minify-exts": []}},
                  open(os.path.join(root, ".nift/config.json"), "w"))
        json.dump({"tracked": [{"name": f"p{i}", "title": f"P{i}", "template": "templates/template.html"}
                               for i in range(20)]}, open(os.path.join(root, ".nift/tracked.json"), "w"))
        open(os.path.join(root, "templates/template.html"), "w").write("<html>@content</html>\n")
        src = [os.path.join(root, f"content/p{i}.html") for i in range(20)]
        for i, p in enumerate(src):
            open(p, "w").write(f"{i}\n")
        init = subprocess.run([nift, "build", "--all"], cwd=root, capture_output=True, text=True)
        if init.returncode != 0:
            fails.append(f"settle initial build failed rc={init.returncode}")
        # deterministic separation from the initial build: sources, then page-info, before the clock
        now = ticks.clock_now(root)
        for p in src + [os.path.join(root, "templates/template.html")]:
            os.utime(p, ns=(now - 2000, now - 2000))
        for f in infos(root):
            os.utime(f, ns=(now - 1000, now - 1000))
        for i, p in enumerate(src):
            open(p, "w").write(f"E{i}\n")
        floor = ticks.sequence(src) if edit == "sequenced" else max(os.stat(p).st_mtime_ns for p in src)
        before = infos(root)
        first = count(root)
        for f, m in infos(root).items():  # the fastest possible first build
            if before.get(f) != m:
                os.utime(f, ns=(floor, floor))
        return [first] + [count(root) for _ in range(3)]
    finally:
        shutil.rmtree(root, ignore_errors=True)


got = settle("unsequenced")
if got[0] != 20 or got[1] < 1 or any(got[2:]):
    fails.append(f"forced race, settle with unsequenced edits: {got}, wanted [20, >=1, 0, 0] (the original [20 1 0 ...])")
got = settle("sequenced")
if got != [20, 0, 0, 0]:
    fails.append(f"forced race, settle with sequenced edits: {got}, wanted [20, 0, 0, 0]")

if fails:
    print("FAIL CP01 tick order:\n  " + "\n  ".join(fails))
    sys.exit(1)
print("PASS CP01 tick order: the forced same-tick race reproduces the original extra rebuilds with "
      "unsequenced edits (ordinary S4a/S4e/S8e, settle [20 1 0 ...]) and none with sequenced edits")
