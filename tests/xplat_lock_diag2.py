# Diagnostic only (xplat-diagnose workflow; not for merge). Windows: when a
# build is killed while its post-build hook runs, which processes exist, which
# PID the build recorded, and whether the hook outlives the killed build.
import ctypes, json, os, pathlib, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_lock_orphan_hook as t

HOOK = """import os, time
from pathlib import Path
p = Path('.')
(p / 'hookpid').write_text('%d %d' % (os.getpid(), os.getppid()))
(p / 'ready').touch()
end = time.monotonic() + 60
while not (p / 'finish').exists():
    if time.monotonic() > end:
        raise SystemExit('hook timeout')
    time.sleep(0.01)
(p / 'done').touch()
"""


def alive(pid):
    # msys2 python has no ctypes.windll: ask Windows via PowerShell (Windows PIDs only).
    ps = "$p = Get-Process -Id %d -ErrorAction SilentlyContinue; if ($p) { 'running ' + $p.ProcessName } else { 'gone' }" % int(pid)
    return subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True).stdout.strip()


def tree():
    ps = ("Get-CimInstance Win32_Process | Where-Object { $_.Name -match 'nift|python|cmd|sh|bash' } | "
          "Select-Object ProcessId,ParentProcessId,Name,CommandLine | ConvertTo-Json -Compress")
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True).stdout
    try:
        rows = json.loads(out)
    except ValueError:
        return out[:2000]
    rows = rows if isinstance(rows, list) else [rows]
    return "\n".join("  %6s <- %6s %s %s" % (r["ProcessId"], r["ParentProcessId"], r["Name"],
                                               (r.get("CommandLine") or "")[:160]) for r in rows)


binary = str(pathlib.Path(sys.argv[1]).resolve())


def winpid_of_nift():
    ps = ("Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'nift.exe' } | "
          "Select-Object -ExpandProperty ProcessId")
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True).stdout.split()
    return out[0] if out else None


def scenario(label, interpreter, kill_mode):
    print("\n===== %s: hook interpreter %s, kill by %s" % (label, interpreter, kill_mode))
    with tempfile.TemporaryDirectory() as td:
        p = t.project(pathlib.Path(td), "diag2")
        (p / "hook.py").write_text(HOOK)
        (p / "post.f").write_text("r := run(" + json.dumps(interpreter) + ', "hook.py")\n')
        proc = subprocess.Popen([binary, "build", "--all"], cwd=p, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        t.wait_for(p, "ready", proc)
        rec = p / ".nift/.unfinished.children"
        record = rec.read_text().split() if rec.exists() else []
        print("recorded", record)
        print("process tree at ready:\n" + tree())
        if kill_mode == "taskkill":
            w = winpid_of_nift()
            print("taskkill /F /PID", w, subprocess.run(["cmd", "/c", "taskkill /F /PID " + str(w)],
                                                        capture_output=True, text=True).stdout.strip())
        else:
            proc.kill()
        proc.communicate()
        time.sleep(1)
        print("after kill: recorded (Windows pid)", alive(record[0]) if record else "-")
        print("process tree after kill:\n" + tree())
        r = subprocess.run([binary, "build"], cwd=p, capture_output=True, text=True, timeout=120)
        print("second build rc", r.returncode, (r.stderr.strip().splitlines() or [""])[0][:160])
        (p / "finish").touch()
        deadline = time.monotonic() + 10
        while not (p / "done").exists() and time.monotonic() < deadline:
            time.sleep(0.1)
        print("orphan hook wrote done after finish:", (p / "done").exists())
        print("recorded pid at end:", alive(record[0]) if record else "-")


msys_python = t.python_tool()
native = os.environ.get("pythonLocation", "").rstrip("\\/") + "\\python.exe"
native_posix = subprocess.run(["cygpath", "-u", native], capture_output=True, text=True).stdout.strip()
print("msys python:", msys_python, "| native python:", native, os.path.exists(native_posix))
scenario("A", msys_python, "msys-kill")
scenario("B", msys_python, "taskkill")
if os.path.exists(native_posix):
    scenario("C", native, "msys-kill")
    scenario("D", native, "taskkill")
