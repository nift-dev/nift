#!/usr/bin/env python3
"""CP23 x CP19 combined-tree witnesses (integration, 2026-10-29).

Adversarial scenarios that mix CP23 parallel tasks with CP17/CP19 managed
resources and CP19/CP21 shell jobs, on the integrated tree. Every script case
runs with NIFT_VM=require at 1 and 4 CPU permits and must be counted as one VM
execution and zero legacy executions (NIFT_VM_STATS), so a legacy fallback can
never pass for combined behaviour.

Script cases (managed file "keep.txt" holds unsaved changes in each):
  outstanding-task  an unawaited CPU-bound task is still running at the end:
                    the run settles exactly like the task-free baseline (exit 1,
                    one "managed file left open" report, file unchanged).
  cancel-blocked    a task blocked in recv() is cancelled and awaited: the
                    catchable runtime.cancelled reaches the script, the script
                    continues, then the file is reported once at the end.
  error-unwind      an uncaught script Error after cancelling a blocked task:
                    the Error is reported, the file is discarded silently
                    (D-056 exceptional abandonment), and the process ends.
  task-error-save   a task's script Error is caught at await; the file is
                    then saved: its write lands exactly once ("rw" overwrites in place,
                    so "original" becomes "dirtynal").
Shell cases (interactive session on stdin, NIFT_VM=1 and require):
  job-blocked-script  a background job runs a script whose parallel task is
                    blocked forever; the session refuses a coroutine command
                    atomically (session:coroutine), continues, and at EOF the
                    job table terminates the job: the script process is gone,
                    the session's own managed file is reported once.
  job-exit-status   the same, ending with `exit 7`: the session finishes as
                    cancelled (accepted CP19 semantics), the file is discarded
                    silently, and the job is still terminated.
Eval cases (NIFT_VM=1 and require, 10 runs each; integration review F1): a
worker's top-level return never becomes the `nift eval` result.
"""
import argparse
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

LEFT_OPEN = re.compile(r"managed file left open[^\n]*keep\.txt")
SPIN = 'fn[async](spin(n)){i:=0;s:=0;while(i<n){s+=i;i+=1};return s};'
BLOCKED = 'c:=channel(0);fn[async](blocked()){recv(c);return 1};'
DIRTY = 'f:=file("keep.txt");f.open("rw");f.write("dirty");'

SCRIPTS = {
    "outstanding-task": (DIRTY + SPIN + 'p:=spin(3000000);q:=spin(10);print(await q)',
                         DIRTY + SPIN + 'q:=spin(10);print(await q)'),
    "cancel-blocked": (DIRTY + BLOCKED + 'p:=blocked();sleep(10);cancel(p);try{await p}catch(e){print(e.code)};'
                       'print("end")', None),
    "error-unwind": (DIRTY + BLOCKED + 'p:=blocked();sleep(10);cancel(p);throw error("boom","user.boom")', None),
    "task-error-save": (DIRTY + 'fn[async](bad()){throw error("inner","user.inner")};p:=bad();'
                        'try{await p}catch(e){print(e.code)};f.save();f.close();print("end")', None),
}
EXPECT = {  # (rc, stdout, managed-file reports, final file content, stderr pattern)
    "outstanding-task": (1, "45\n", 1, "original", None),
    "cancel-blocked": (1, "runtime.cancelled\nend\n", 1, "original", None),
    "error-unwind": (1, "", 0, "original", r"error: [^\n]*: boom\n"),
    "task-error-save": (0, "user.inner\nend\n", 0, "dirtynal", None),  # "rw" overwrites in place
}


def run_script(binary, source, workers, td):
    root = Path(td)
    (root / "keep.txt").write_text("original")
    (root / "case.f").write_text(source)
    stats = root / "stats.jsonl"
    stats.unlink(missing_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(NIFT_VM="require", NIFT_VM_PARALLEL_WORKERS=str(workers), NIFT_VM_STATS=str(stats), NO_COLOR="1")
    t0 = time.monotonic()
    p = subprocess.run([binary, "case.f"], cwd=td, env=env, capture_output=True, text=True, timeout=60)
    st = json.loads(stats.read_text().splitlines()[-1]) if stats.exists() and stats.read_text().strip() else {}
    return {"rc": p.returncode, "stdout": p.stdout, "stderr": p.stderr, "seconds": round(time.monotonic() - t0, 3),
            "file": (root / "keep.txt").read_text(), "vm": st.get("vm_executions"), "legacy": st.get("legacy_executions")}


def check_script(case, r, base):
    rc, out, reports, content, pattern = EXPECT[case]
    problems = []
    if r["rc"] != rc: problems.append(f"rc {r['rc']}, want {rc}")
    if r["stdout"] != out: problems.append(f"stdout {r['stdout']!r}, want {out!r}")
    n = len(LEFT_OPEN.findall(r["stderr"]))
    if n != reports: problems.append(f"{n} managed-file reports, want {reports}")
    if r["file"] != content: problems.append(f"file {r['file']!r}, want {content!r}")
    if pattern and not re.search(pattern, r["stderr"]): problems.append(f"stderr lacks {pattern!r}")
    if "terminate called" in r["stderr"] or r["rc"] < 0: problems.append("aborted")
    if (r["vm"], r["legacy"]) != (1, 0): problems.append(f"executions vm={r['vm']} legacy={r['legacy']}, want 1/0")
    if base is not None:
        for key in ("rc", "stdout", "file"):
            if r[key] != base[key]: problems.append(f"{key} differs from the task-free baseline")
        if len(LEFT_OPEN.findall(base["stderr"])) != n: problems.append("report count differs from baseline")
    return problems


def alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    try:
        return Path(f"/proc/{pid}/stat").read_text().split(")")[-1].split()[0] != "Z"
    except OSError:
        return True


def run_shell(binary, mode, case, td):
    root = Path(td)
    (root / "keep.txt").write_text("original")
    (root / "blocked.f").write_text(DIRTY.replace("keep.txt", "child.txt") + BLOCKED + 'p:=blocked();sleep(10);print("child")')
    (root / "child.txt").write_text("child-original")
    (root / "wrap.sh").write_text(f'echo $$ > "{root}/child.pid"\n'
                                  f'exec env NIFT_VM=require NIFT_VM_PARALLEL_WORKERS=2 "{binary}" "{root}/blocked.f"\n')
    lines = [DIRTY.rstrip(";"),
             f'sh {root / "wrap.sh"} &',
             f'sh -c "while [ ! -s {root / "child.pid"} ]; do sleep 0.05; done; sleep 0.3"',
             'fn[async](g()){return 6}',
             'print("after")']
    if case == "job-exit-status":
        lines.append("exit 7")
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(HOME=td, NO_COLOR="1", NIFT_VM=mode)
    p = subprocess.run([binary], input="\n".join(lines) + "\n", cwd=td, env=env, capture_output=True, text=True,
                       timeout=60)
    pidfile = root / "child.pid"
    pid = int(pidfile.read_text().strip()) if pidfile.exists() and pidfile.read_text().strip() else None
    deadline = time.monotonic() + 3
    while pid is not None and alive(pid) and time.monotonic() < deadline:
        time.sleep(0.05)
    survived = pid is not None and alive(pid)
    if survived:
        os.kill(pid, signal.SIGKILL)
    return {"rc": p.returncode, "stdout": p.stdout, "stderr": p.stderr, "child_pid": pid, "child_survived": survived,
            "file": (root / "keep.txt").read_text(), "child_file": (root / "child.txt").read_text()}


def check_shell(case, r):
    problems = []
    err = r["stderr"]
    if r["child_pid"] is None: problems.append("job never started")
    if r["child_survived"]: problems.append("script job survived the session")
    if r["child_file"] != "child-original": problems.append("the job's unsaved changes were written")
    if err.count("NIFT_VM session refused: session:coroutine") != 1:
        problems.append("coroutine command not refused exactly once with session:coroutine")
    if "after" not in r["stdout"]: problems.append("session did not continue after the refusal")
    if "child" in r["stdout"].split(): problems.append("the blocked script completed")
    if r["file"] != "original": problems.append("session's unsaved changes were written")
    if "terminate called" in err or r["rc"] < 0: problems.append("aborted")
    n = len(LEFT_OPEN.findall(err))
    if case == "job-blocked-script":
        if r["rc"] != 1: problems.append(f"rc {r['rc']}, want 1")
        if n != 1: problems.append(f"{n} managed-file reports, want 1")
    else:
        if r["rc"] != 7: problems.append(f"rc {r['rc']}, want 7")
        if n != 0: problems.append(f"{n} managed-file reports, want 0 (cancelled session)")
    return problems


EVAL_CASES = {  # integration review F1: a worker's top-level return never becomes the eval result
    "eval-worker-result": ('[thread(() => {sleep(30); return "worker"}), "parent"][1]', "parent"),
    "eval-async-worker-result": ('[thread(() => {return "worker"}).join(), "parent"][1]', "parent"),
}


def run_eval(binary, mode, expr, repeats=10):
    outs = []
    for _ in range(repeats):
        env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
        env.update(NIFT_VM=mode, NO_COLOR="1")
        p = subprocess.run([binary, "eval", expr], env=env, capture_output=True, text=True, timeout=60)
        outs.append((p.returncode, p.stdout.strip(), p.stderr.strip()))
    return outs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--switch", required=True, help="switch-dispatch binary")
    ap.add_argument("--output", required=True)
    a = ap.parse_args()
    rows, failures = [], 0
    for binary in (os.path.abspath(a.nift), os.path.abspath(a.switch)):
        for workers in (1, 4):
            for case, (source, baseline) in SCRIPTS.items():
                with tempfile.TemporaryDirectory(prefix="cp23-cp19-") as td:
                    r = run_script(binary, source, workers, td)
                with tempfile.TemporaryDirectory(prefix="cp23-cp19-base-") as td:
                    base = run_script(binary, baseline, workers, td) if baseline else None
                problems = check_script(case, r, base)
                rows.append({"binary": binary, "workers": workers, "case": case, "result": r, "problems": problems})
                label = f"{Path(binary).name} permits={workers} {case}"
                if problems:
                    failures += 1
                    print(f"FAIL {label}: " + "; ".join(problems) + f"\n  stderr: {r['stderr'][-300:]!r}")
                else:
                    print(f"ok   {label}: rc {r['rc']}, VM 1 / legacy 0, {r['seconds']} s")
        for mode in ("1", "require"):
            for case in ("job-blocked-script", "job-exit-status"):
                with tempfile.TemporaryDirectory(prefix="cp23-cp19-shell-") as td:
                    r = run_shell(binary, mode, case, td)
                problems = check_shell(case, r)
                rows.append({"binary": binary, "mode": mode, "case": case, "result": r, "problems": problems})
                label = f"{Path(binary).name} NIFT_VM={mode} {case}"
                if problems:
                    failures += 1
                    print(f"FAIL {label}: " + "; ".join(problems) + f"\n  stderr: {r['stderr'][-300:]!r}")
                else:
                    print(f"ok   {label}: rc {r['rc']}, job terminated, refusal atomic")
        for mode in ("1", "require"):
            for case, (expr, want) in EVAL_CASES.items():
                outs = run_eval(binary, mode, expr)
                bad = [o for o in outs if o != (0, want, "")]
                rows.append({"binary": binary, "mode": mode, "case": case, "result": outs, "problems": bad})
                label = f"{Path(binary).name} NIFT_VM={mode} {case}"
                if bad:
                    failures += 1
                    print(f"FAIL {label}: {len(bad)}/{len(outs)} runs differ from {want!r}, e.g. {bad[0]!r}")
                else:
                    print(f"ok   {label}: {len(outs)}/{len(outs)} runs {want!r}")
    Path(a.output).write_text(json.dumps({"rows": rows, "failures": failures}, indent=1) + "\n")
    print(f"{'PASS' if not failures else 'FAIL'} CP23 x CP19 combined: {len(rows)} cases, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
