#!/usr/bin/env python3
"""CP19 shell-loop exception boundary (2026-10-29).

Before the correction, an exception escaping the interactive shell loop had
no handler anywhere, so std::terminate ran without unwinding: exit 134, no
"managed file left open" diagnostic, no job-table cleanup (Linux jobs died
only through PR_SET_PDEATHSIG). A deleted working directory triggered it with
no fault injection: the prompt called the throwing fs::current_path().

Each case runs with a managed file holding unsaved changes and a background
job at the same time, in legacy (NIFT_VM=0) and VM-session (NIFT_VM=1,
NIFT_VM=require) modes:

  natural   production binary: mkdir/cd/rmdir the working directory. The
            prompt must not end the shell (it shows "?"): the next statement
            runs, and at EOF both domains settle normally: exit 1, one
            managed-file diagnostic, file unchanged, job terminated by the job
            table. Scope: a statement that itself needs the deleted directory
            (cd .., pwd, exists(...)) still raises from a throwing
            std::filesystem call; the boundary then ends the session cleanly
            with status 70 instead of aborting (independent review F1; making
            those helpers non-throwing is a separate follow-up).
  inject-*  test-only binary (NIFT_TEST_SHELL_THROW) throwing
            std::runtime_error, std::bad_alloc or a non-std value from inside
            the loop. Exit 70, one "internal error" diagnostic naming it, the
            next statement never runs, and both domains still settle exactly
            once. Legacy reports the open file. A VM session ending with a
            failure finishes as cancelled (accepted CP19 semantics, as for an
            explicit nonzero exit) and discards the file silently. In every
            mode the unsaved changes are never written.
  control   the test-only binary without injection behaves like production,
            and the production binary does not contain the hook.

The job traps SIGTERM and writes a marker: the job table's cleanup terminates
with SIGTERM first, while the kernel backstop (PDEATHSIG) sends an
uncatchable SIGKILL, so the marker proves the job table itself ran.
"""
import argparse
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

JOB = ("trap 'echo term > \"$1\"; exit 0' TERM\n"
       "echo $$ > \"$2\"\n"
       "while :; do sleep 0.1; done\n")
EXPECT_INTERNAL = {
    "inject-runtime": "internal error: injected shell-loop failure",
    "inject-bad-alloc": "internal error: std::bad_alloc",
    "inject-int": "internal error: unknown exception",
}
MAGIC = {
    "inject-runtime": "__nift_test_throw_runtime__",
    "inject-bad-alloc": "__nift_test_throw_bad_alloc__",
    "inject-int": "__nift_test_throw_int__",
}


def alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    # a zombie is not alive for this purpose
    try:
        with open(f"/proc/{pid}/stat") as f:
            return f.read().split(")")[-1].split()[0] != "Z"
    except OSError:
        return True


def run_case(binary, mode, case):
    with tempfile.TemporaryDirectory(prefix="cp19-shell-exc-") as td:
        root = Path(td)
        (root / "keep.txt").write_text("original")
        (root / "job.sh").write_text(JOB)
        term, pidfile = root / "term.txt", root / "job.pid"
        lines = ['f := file("keep.txt");f.open("rw");f.write("dirty")',
                 f'sh {root / "job.sh"} {term} {pidfile} &',
                 # Foreground wait until the job has installed its TERM trap
                 # (it writes its pid after the trap), so the failure cannot
                 # race the job's startup.
                 f'sh -c "while [ ! -s {pidfile} ]; do sleep 0.05; done"']
        if case == "natural":
            lines += ["mkdir gone", "cd gone", "rmdir ../gone"]
        elif case == "natural-cwd-builtin":  # independent review F1
            lines += ["mkdir gone", "cd gone", "rmdir ../gone", "cd .."]
        elif case in MAGIC:
            lines += [MAGIC[case]]
        lines += ['print("after")']
        env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
        env.update(HOME=td, NO_COLOR="1")
        if mode != "default":
            env["NIFT_VM"] = mode
        p = subprocess.run([binary], input="\n".join(lines) + "\n", cwd=td, env=env, text=True,
                           capture_output=True, timeout=60)
        job_pid = int(pidfile.read_text().strip()) if pidfile.exists() and pidfile.read_text().strip() else None
        time.sleep(0.3)
        survived = job_pid is not None and alive(job_pid)
        if survived:
            os.kill(job_pid, signal.SIGKILL)
        return {"rc": p.returncode, "stdout": p.stdout, "stderr": p.stderr, "job_pid": job_pid,
                "job_survived": survived, "job_term_marker": term.exists(),
                "file": (root / "keep.txt").read_text()}


def check(case, r, mode):
    problems = []
    err = r["stderr"]
    left_open = re.findall(r"managed file left open[^\n]*keep\.txt[^\n]*", err)
    if "terminate called" in err or r["rc"] in (134, -6):
        problems.append(f"aborted (rc {r['rc']})")
    # Legacy reports a file left open by any loop exit. A VM session that ends
    # with a failure finishes as cancelled (accepted CP19 semantics, as for an
    # explicit nonzero exit): the file is discarded silently. Either way it is
    # finalized exactly once and its unsaved changes are never written.
    want_reports = 0 if (case in EXPECT_INTERNAL and mode != "0") else 1
    if len(left_open) != want_reports:
        problems.append(f"{len(left_open)} managed-file diagnostics, want exactly {want_reports}")
    if r["file"] != "original":
        problems.append(f"file content {r['file']!r}, want unsaved changes discarded")
    if r["job_pid"] is None:
        problems.append("job never started")
    if r["job_survived"]:
        problems.append("background job survived the shell")
    if not r["job_term_marker"]:
        problems.append("job not terminated by the job table (no SIGTERM marker)")
    internal = re.findall(r"internal error:[^\n]*", err)
    if case == "natural-cwd-builtin":
        # A statement that needs the deleted directory still raises; the
        # session ends cleanly with 70 (legacy through the shell boundary,
        # the VM through its own internal-failure handling), never an abort.
        if r["rc"] != 70:
            problems.append(f"rc {r['rc']}, want 70")
        if not re.search(r"internal (error|failure)", err):
            problems.append("no internal error/failure diagnostic")
        if "after" in r["stdout"]:
            problems.append("statement after the failure ran")
        return [x for x in problems if "managed-file diagnostics" not in x] + (
            [] if mode != "0" or len(left_open) == 1 else [f"{len(left_open)} managed-file diagnostics, want 1"])
    if case in EXPECT_INTERNAL:
        if r["rc"] != 70:
            problems.append(f"rc {r['rc']}, want 70")
        if internal != [EXPECT_INTERNAL[case]]:
            problems.append(f"internal diagnostics {internal}, want [{EXPECT_INTERNAL[case]!r}]")
        if "after" in r["stdout"]:
            problems.append("statement after the failure ran")
    else:
        if r["rc"] != 1:
            problems.append(f"rc {r['rc']}, want 1")
        if internal:
            problems.append(f"unexpected internal diagnostics {internal}")
        if "after" not in r["stdout"]:
            problems.append("shell did not continue to the next statement")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True, help="production binary")
    ap.add_argument("--inject", required=True, help="NIFT_TEST_SHELL_THROW binary")
    a = ap.parse_args()
    failures = 0
    for mode in ("0", "1", "require"):
        plan = [("natural", a.nift), ("natural-cwd-builtin", a.nift), ("control", a.inject)] + \
               [(c, a.inject) for c in MAGIC]
        for case, binary in plan:
            r = run_case(binary, mode, case)
            problems = check(case, r, mode)
            label = f"NIFT_VM={mode} {case}"
            if problems:
                failures += 1
                print(f"FAIL {label}: " + "; ".join(problems) + f"\n  stderr: {r['stderr'][-400:]!r}")
            else:
                print(f"ok   {label}: rc {r['rc']}, job terminated by the job table, file unchanged")
    # The hook must not exist in the production binary.
    r = run_case(a.nift, "0", "inject-runtime")
    if re.search(r"internal error", r["stderr"]):
        failures += 1
        print("FAIL production binary contains the test-only failure injection")
    else:
        print("ok   production binary has no failure-injection hook")
    print(f"{'PASS' if not failures else 'FAIL'} CP19 shell exception boundary: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
