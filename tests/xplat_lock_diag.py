# Diagnostic only (xplat-diagnose workflow; not for merge): what the build
# records on Windows when it is killed while its post-build hook runs.
import os, pathlib, subprocess, sys, time, json, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_lock_orphan_hook as t
binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as td:
    p = t.project(pathlib.Path(td), "diag")
    proc = subprocess.Popen([binary, "build", "--all"], cwd=p, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    t.wait_for(p, "ready", proc)
    print("nift pid", proc.pid)
    print(".nift before kill:", sorted(x.name for x in (p / ".nift").iterdir()))
    rec = p / ".nift/.unfinished.children"
    print("children record:", rec.read_text() if rec.exists() else "(missing)")
    proc.kill(); proc.communicate()
    print(".nift after kill:", sorted(x.name for x in (p / ".nift").iterdir()))
    print("children record:", rec.read_text() if rec.exists() else "(missing)")
    if rec.exists():
        for line in rec.read_text().split("\n"):
            if line.strip():
                pid = line.split()[0]
                print("tasklist", pid, subprocess.run(["tasklist", "/FI", "PID eq " + pid], capture_output=True, text=True).stdout.strip().splitlines()[-1:])
    r = subprocess.run([binary, "build"], cwd=p, capture_output=True, text=True)
    print("second build rc", r.returncode, r.stdout.strip()[-200:], r.stderr.strip()[-300:])
    (p / "finish").touch()
    t.wait_for(p, "done")
