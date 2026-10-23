#!/usr/bin/env python3
"""CP20: shell external commands on Host ProcessGroup, streaming and parity.

Runs each fixture through Nift's non-interactive shell and through bash -c
where a direct comparison is meaningful, checking stdout, expected files and
selected exit statuses. The separate parity probe compares stderr/status with
a pre-CP20 binary; approved D5 message differences are recorded there. Streaming measurements use a three-stage external pipeline and
require the first line within 300 ms, 20 times.
"""
import argparse
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--nift", default="./nift")
parser.add_argument("--mutant-kind", choices=("m1", "m2", "m3", "m4", "m5", "m6"))
args = parser.parse_args()
NIFT = str(Path(args.nift).resolve())

failures = []


def check(name, ok, detail=""):
    if ok:
        print("PASS", name)
    else:
        print("FAIL", name + (": " + detail if detail else ""))
        failures.append(name)


def shell(script, cwd, timeout=30):
    return subprocess.run([NIFT], input=script, cwd=cwd, capture_output=True,
                          text=True, timeout=timeout, check=False)


def bash(script, cwd, timeout=30):
    return subprocess.run(["/bin/bash", "-c", script], cwd=cwd, capture_output=True,
                          text=True, timeout=timeout, check=False)


def one(name, script, expected_stdout=None, expected_rc=None, files=None, inputs=None, compare_bash=False):
    with tempfile.TemporaryDirectory(prefix="cp20-") as tmp:
        root = Path(tmp)
        for rel, data in (inputs or {}).items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data)
        n = shell(script + "\nexit\n", root)
        b = bash(script, root) if compare_bash else None
        ok = True
        why = []
        if expected_stdout is not None and n.stdout != expected_stdout:
            ok = False; why.append(f"stdout={n.stdout!r}")
        if expected_rc is not None and n.returncode != expected_rc:
            ok = False; why.append(f"rc={n.returncode}")
        if b is not None and n.stdout != b.stdout:
            ok = False; why.append(f"bash stdout={b.stdout!r}")
        if files:
            for rel, data in files.items():
                got = (root / rel).read_text() if (root / rel).exists() else None
                if got != data:
                    ok = False; why.append(f"{rel}={got!r}")
        check(name, ok, "; ".join(why))


def main():
    # Basic single, two-stage and three-stage external commands.
    one("echo", "/bin/echo hello", "hello\n", compare_bash=True)
    one("pipe2", "/bin/printf 'a\\nb\\n' | /usr/bin/tr a-z A-Z", "A\nB\n", compare_bash=True)
    one("pipe3", "/bin/echo hi | /bin/cat | /bin/cat", "hi\n", compare_bash=True)

    # Redirection forms.
    one("stdout-truncate", "/bin/echo one > out.txt", files={"out.txt": "one\n"})
    one("stdout-append", "/bin/echo one > out.txt\n/bin/echo two >> out.txt", files={"out.txt": "one\ntwo\n"})
    one("stderr-file", "/bin/sh -c 'echo out; echo err >&2' 2> err.txt", "out\n", files={"err.txt": "err\n"})
    one("stderr-append", "/bin/sh -c 'echo e1 >&2' 2> err.txt\n/bin/sh -c 'echo e2 >&2' 2>> err.txt",
        files={"err.txt": "e1\ne2\n"})
    one("merge-file", "/bin/sh -c 'echo out; echo err >&2' > all.txt 2>&1", files={"all.txt": "out\nerr\n"})
    one("merge-pipe", "/bin/sh -c 'echo out; echo err >&2' 2>&1 | /usr/bin/tr a-z A-Z", "OUT\nERR\n")
    one("input", "/bin/cat < in.txt", "input\n", inputs={"in.txt": "input\n"})

    # Environment assignment scope.
    one("env-child", "FOO=bar /bin/sh -c 'printf %s \"$FOO\"'", "bar", compare_bash=True)
    one("env-scope", "FOO=bar /usr/bin/true\nprint(getenv(\"FOO\") == null)", "true\n")

    # Missing executable message/status path (the shell keeps its own status).
    with tempfile.TemporaryDirectory(prefix="cp20-missing-") as tmp:
        n = shell("definitely-not-a-cp20-command\n", Path(tmp), timeout=20)
        check("missing-command", "command not found" in (n.stdout + n.stderr), n.stdout + n.stderr)

    # Background execution, wait and the job list.
    with tempfile.TemporaryDirectory(prefix="cp20-bg-") as tmp:
        root = Path(tmp)
        n = shell("sleep 0.15 &\nwait\ntouch bg-done\njobs\n", root, timeout=20)
        check("background-wait", (root / "bg-done").exists(), n.stdout + n.stderr)
        check("background-jobs", "Done (0)" in n.stdout, n.stdout + n.stderr)

    # A background wait must actually reap the group before the next command.
    with tempfile.TemporaryDirectory(prefix="cp20-bg-left-") as tmp:
        root = Path(tmp)
        n = shell("/bin/sh -c 'sleep 0.4; touch bg-marker' &\nwait\n"
                  "/bin/sh -c 'test -f bg-marker && echo WAITED || echo EARLY'\n", root, timeout=20)
        check("background-leftover", "WAITED" in n.stdout and "EARLY" not in n.stdout,
              n.stdout + n.stderr)

    # Script job control (B0): foreground IDs, jobs, bg/fg and stopped state.
    with tempfile.TemporaryDirectory(prefix="cp20-jobs-") as tmp:
        root = Path(tmp)
        n = shell("printf dogfood | cat\nsleep 0.03 &\nwait 2\njobs\nexit\n", root)
        check("script-job-ids", "error:" not in n.stderr and "[1] Done (0)" in n.stdout
              and "[2] Done (0)" in n.stdout, n.stdout + n.stderr)
        n = shell("sh -c 'kill -STOP $$; sleep 0.03' &\nsleep 0.03\nbg 1\nwait 1\njobs\nexit\n", root)
        check("script-bg-resume", "[1] Done (0)" in n.stdout and "unknown job" not in n.stderr,
              n.stdout + n.stderr)
        n = shell("sh -c 'kill -STOP $$; sleep 0.03' &\nsleep 0.05\njobs\nbg 1\nwait 1\njobs\nexit\n", root)
        check("script-stopped", "Stopped" in n.stdout and "[1] Done (0)" in n.stdout,
              n.stdout + n.stderr)
        n = shell("sh -c 'kill -STOP $$; sleep 0.03' &\nsleep 0.03\nfg 1\njobs\nexit\n", root)
        check("script-fg-resume", "[1] Done (0)" in n.stdout and "unknown job" not in n.stderr,
              n.stdout + n.stderr)

    # B0 M8: bg must actually SIGCONT the stopped script job. Observe the
    # child state instead of waiting on a group that may remain stopped.
    with tempfile.TemporaryDirectory(prefix="cp20-bgstate-") as tmp:
        proc = subprocess.Popen([NIFT], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, cwd=tmp)
        proc.stdin.write(b"sh -c 'kill -STOP $$; sleep 2' &\nsleep 0.2\nbg 1\nsleep 0.2\n")
        proc.stdin.flush()
        time.sleep(0.7)
        stopped = []
        for stat_path in Path("/proc").glob("[0-9]*/stat"):
            try:
                fields = stat_path.read_text().split()
                if len(fields) > 3 and int(fields[3]) == proc.pid and fields[2] == "T":
                    stopped.append(int(fields[0]))
            except (OSError, ValueError):
                pass
        check("script-bg-state", not stopped, f"stopped_children={stopped}")
        subprocess.run(["/bin/true"], check=False)
        proc.kill(); proc.wait(timeout=5)

    # Genuine zombie/never-reaped check (scripted Host background path).
    with tempfile.TemporaryDirectory(prefix="cp20-zombie-") as tmp:
        proc = subprocess.Popen([NIFT], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, cwd=tmp)
        proc.stdin.write(b"/bin/true &\nwait\n/bin/sleep 0.5\nexit\n")
        proc.stdin.close()
        time.sleep(0.25)
        zombies = []
        for stat_path in Path("/proc").glob("[0-9]*/stat"):
            try:
                fields = stat_path.read_text().split()
                if len(fields) > 3 and int(fields[3]) == proc.pid and fields[2] == "Z":
                    zombies.append(int(fields[0]))
            except (OSError, ValueError):
                pass
        proc.wait(timeout=5)
        check("background-zombie", not zombies, f"zombies={zombies}")

    # Pipeline status uses the last stage; the middle failure must not win.
    one("pipeline-last-status", "/bin/sh -c 'exit 7' | /usr/bin/true && echo LAST_OK", "LAST_OK\n")
    one("pipeline-last-status-fail", "/usr/bin/true | /usr/bin/false && echo BAD || echo LAST_FAIL",
        "LAST_FAIL\n")

    # Mutant M6: middle-stage nonzero must not become the pipeline status.
    if args.mutant_kind == "m6":
        with tempfile.TemporaryDirectory(prefix="cp20-m6-") as tmp:
            n = shell("/usr/bin/true | /usr/bin/false | /usr/bin/true && echo OK\n", Path(tmp))
            check("mutant-m6", "OK\n" == n.stdout, n.stdout + n.stderr)

    # M2: append must not truncate.
    if args.mutant_kind == "m2":
        with tempfile.TemporaryDirectory(prefix="cp20-m2-") as tmp:
            root = Path(tmp)
            shell("/bin/echo one > out.txt\n/bin/echo two >> out.txt\n", root)
            got = (root / "out.txt").read_text() if (root / "out.txt").exists() else ""
            check("mutant-m2", got == "one\ntwo\n", got)

    # M3: 2>&1 must merge.
    if args.mutant_kind == "m3":
        with tempfile.TemporaryDirectory(prefix="cp20-m3-") as tmp:
            n = shell("/bin/sh -c 'echo out; echo err >&2' 2>&1 | /usr/bin/tr a-z A-Z\n", Path(tmp))
            check("mutant-m3", "OUT\nERR\n" == n.stdout, n.stdout + n.stderr)

    # M4: env assignment must not leak to the shell.
    if args.mutant_kind == "m4":
        with tempfile.TemporaryDirectory(prefix="cp20-m4-") as tmp:
            n = shell("FOO=bar /usr/bin/true\nprint(getenv(\"FOO\") == null)\n", Path(tmp))
            check("mutant-m4", n.stdout == "true\n", n.stdout + n.stderr)

    # M5: wait must reap the background group.
    if args.mutant_kind == "m5":
        with tempfile.TemporaryDirectory(prefix="cp20-m5-") as tmp:
            n = shell("sleep 0.2 &\nwait\njobs\n", Path(tmp), timeout=20)
            check("mutant-m5", "Done (0)" in n.stdout, n.stdout + n.stderr)

    # Streaming: first line must arrive before the producer exits.
    stream_line = "sh -c 'echo first; sleep 1; echo second' | cat | cat"
    for variant, body in (("plain", stream_line), ("merge", stream_line + " 2>&1")):
        bad = 0
        for _ in range(20):
            p = subprocess.Popen([NIFT], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, cwd=tempfile.gettempdir())
            p.stdin.write((body + "\nexit\n").encode()); p.stdin.close()
            start = time.monotonic()
            first = None
            while time.monotonic() - start < 1.0:
                ready, _, _ = select.select([p.stdout], [], [], 0.02)
                if not ready:
                    if p.poll() is not None:
                        break
                    continue
                first = p.stdout.readline()
                break
            if first is None or first.strip() != b"first" or (time.monotonic() - start) > 0.3:
                bad += 1
            p.stdout.read(); p.stderr.read(); p.wait(timeout=5)
        check(f"streaming-{variant}", bad == 0, f"{bad}/20 failures")
        if args.mutant_kind == "m1" and variant == "plain":
            check("mutant-m1", bad > 0, f"{bad}/20 failures")

    if failures:
        print(f"FAIL {len(failures)}")
        return 1
    print("PASS cp20 shell spawn")
    return 0


if __name__ == "__main__":
    sys.exit(main())
