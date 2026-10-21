#!/usr/bin/env python3
"""Build-lock lifetime when a build is killed while its hook still runs.

Contract (owner, 2026-10-21): a build lock must stay effective for as long as
a process the build launched, and which can still modify the build's outputs,
is running. POSIX gets this from the inherited marker flock; Windows records
the build's children (ProjectOwnership.cpp). Checked on every platform:

  1. a killed build's post-build hook keeps running: `nift build` and a
     tracking change (`nift untrack`) are refused while it runs;
  2. once the hook exits, `nift build` recovers and succeeds;
  3. a build whose hook finished normally never blocks the next build;
  4. a stale child record (a live pid with a different creation time, i.e. a
     reused pid) does not block recovery.
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time

from shared_dependency_targeted_reproducer import setup


def python_tool():
    # Nift's native Windows CreateProcess does not apply MSYS path translation.
    if os.environ.get("MSYSTEM"):
        return subprocess.check_output(["cygpath", "-aw", sys.executable], text=True).strip()
    return sys.executable


HOOK = """from pathlib import Path
import time
p = Path('.')
(p / 'ready').touch()
end = time.monotonic() + 60
while not (p / 'finish').exists():
    if time.monotonic() > end:
        raise SystemExit('hook timeout')
    time.sleep(0.01)
(p / 'public' / 'hook.txt').write_text('written by the hook')
(p / 'done').touch()
"""


def project(base, name):
    p = base / name
    p.mkdir()
    setup(p, "hash", "template")
    (p / ".nift/tracked.json").write_text(json.dumps({"tracked": [
        dict(name="a", title="a", template="templates/shared.html", **{"post-build": "post.f"})]}))
    (p / "hook.py").write_text(HOOK)
    (p / "post.f").write_text("r := run(" + json.dumps(python_tool()) + ', "hook.py")\n')
    return p


def nift(binary, p, *args):
    return subprocess.run([binary, *args], cwd=p, capture_output=True, text=True, timeout=120)


def wait_for(p, name, proc=None, seconds=30):
    deadline = time.monotonic() + seconds
    while not (p / name).exists():
        if proc is not None and proc.poll() is not None:
            raise AssertionError(("build exited early", proc.communicate()))
        if time.monotonic() > deadline:
            raise AssertionError("timed out waiting for " + name)
        time.sleep(0.01)


def refused(r):
    return r.returncode != 0 and "another build appears to be running" in (r.stdout + r.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    a = ap.parse_args()
    binary = str(pathlib.Path(a.nift).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix="nift-lock-orphan-") as td:
        base = pathlib.Path(td)

        # 1 and 2: killed build, orphaned hook.
        p = project(base, "killed")
        proc = subprocess.Popen([binary, "build", "--all"], cwd=p, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            wait_for(p, "ready", proc)
            proc.kill()
            proc.communicate()
            assert (p / ".nift/.unfinished").exists(), "killed build left no unfinished marker"
            r = nift(binary, p, "build")
            assert refused(r), ("build not refused while the hook runs", r.returncode, r.stdout, r.stderr)
            checks += 1
            r = nift(binary, p, "untrack", "a")
            assert r.returncode != 0, ("untrack not refused while the hook runs", r.stdout, r.stderr)
            checks += 1
        finally:
            (p / "finish").touch()
        wait_for(p, "done")
        deadline = time.monotonic() + 30
        while True:  # the hook's interpreter may take a moment to exit after writing `done`
            r = nift(binary, p, "build")
            if r.returncode == 0:
                break
            assert refused(r), ("unexpected build failure after the hook exited", r.stdout, r.stderr)
            assert time.monotonic() < deadline, "still refused 30 s after the hook finished"
            time.sleep(0.2)
        assert not (p / ".nift/.unfinished").exists()
        checks += 1

        # 3: a hook that finishes normally never blocks the next build.
        p = project(base, "normal")
        (p / "finish").touch()
        r = nift(binary, p, "build", "--all")
        assert r.returncode == 0, (r.stdout, r.stderr)
        r = nift(binary, p, "build")
        assert r.returncode == 0, ("build after a clean build was refused", r.stdout, r.stderr)
        checks += 1

        # 4: a record whose pid is alive but whose creation time differs (pid
        # reuse) must not block recovery. On POSIX the record is ignored.
        p = project(base, "reused")
        (p / "finish").touch()
        proc = subprocess.Popen([binary, "build", "--all"], cwd=p, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        wait_for(p, "ready", proc)
        proc.kill()
        proc.communicate()
        wait_for(p, "done")
        with open(p / ".nift/.unfinished.children", "a") as f:
            f.write("%d 1\n" % os.getpid())
        deadline = time.monotonic() + 30
        while True:
            r = nift(binary, p, "build")
            if r.returncode == 0:
                break
            assert time.monotonic() < deadline, ("recovery blocked by a reused pid record", r.stdout, r.stderr)
            time.sleep(0.2)
        checks += 1

    print("PASS build lock vs orphaned hook: %d checks" % checks)


if __name__ == "__main__":
    main()
