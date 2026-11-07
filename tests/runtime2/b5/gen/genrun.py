"""Run one generated program on the VM (NIFT_VM=require) and, when the VM admits it, on legacy
(NIFT_VM=0); used by gen1.py and gen_struct.py (B5 generators, driven by gate.py).

Moved into the tree by the B5 fix-up (review F6): it was a scratch-only tool. Each program runs in
its own directory under WORK (the gate's temporary directory), which the gate removes.
"""
import json
import os
import shutil
import subprocess
import tempfile

WORK = None  # set by the gate (set_work) before any program runs


def set_work(path):
    global WORK
    WORK = path
    os.makedirs(WORK, exist_ok=True)


def env_for(d, mode, stats):
    e = {"PATH": "/usr/bin:/bin", "HOME": d, "TMPDIR": d, "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "NO_COLOR": "1",
         "NIFT_VM": mode}
    # a sanitizer build's options (the evalorder sanitizer lane) reach the programs
    e.update({k: v for k, v in os.environ.items() if k in ("ASAN_OPTIONS", "UBSAN_OPTIONS", "LSAN_OPTIONS")})
    if stats:
        e["NIFT_VM_STATS"] = stats
    return e


def run(nift, files, mode, main="t.f", stdin=None, args=None):
    if WORK is None:
        raise RuntimeError("genrun.set_work() was not called")
    d = tempfile.mkdtemp(dir=WORK)
    for n, s in files.items():
        with open(os.path.join(d, n), "w") as f:
            f.write(s)
    st = os.path.join(d, "stats.jsonl")
    cmd = ["timeout", "20", nift] + (args if args is not None else [main])
    p = subprocess.run(cmd, cwd=d, env=env_for(d, mode, st), input=(stdin.encode() if stdin else None),
                       stdin=(None if stdin else subprocess.DEVNULL), capture_output=True)
    norm = lambda b: b.decode("utf-8", "replace").replace(d + os.sep, "").replace(d, "<D>")
    rows = []
    if os.path.exists(st):
        for line in open(st):
            try:
                rows.append(json.loads(line))
            except ValueError:
                pass
    shutil.rmtree(d, ignore_errors=True)
    return norm(p.stdout), norm(p.stderr), p.returncode, rows


def vm_vs_legacy(nift, files, main="t.f", stdin=None, args=None):
    vo, ve, vc, rows = run(nift, files, "require", main, stdin, args)
    refused = any(r.get("outcome") == "require-error" for r in rows) or "not compiled for the VM" in ve
    defects = sum(int(r.get("dispatch_defects", 0)) for r in rows)
    res = {"vm": (vo, ve, vc), "refused": refused, "defects": defects,
           "reason": [r.get("reason") for r in rows if r.get("outcome") == "require-error"],
           "outcomes": [r.get("outcome") for r in rows]}
    if not refused:
        lo, le, lc, _ = run(nift, files, "0", main, stdin, args)
        res["legacy"] = (lo, le, lc)
    return res
