#!/usr/bin/env python3
"""CP20 R1: real-PTY job control, fd limits, leftovers, FS_ROOT and D5."""
import argparse
import os
import pty
import resource
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--nift", required=True)
args = parser.parse_args()
NIFT = str(Path(args.nift).resolve())
failures = []


def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else f": {detail}"))
    if not ok:
        failures.append(name)


def pty_session(lines, waits=0.35, timeout=8, extra_env=None):
    home = tempfile.mkdtemp(prefix="cp20-pty-home-")
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["HOME"] = home
        os.environ["TERM"] = "dumb"
        if extra_env:
            os.environ.update(extra_env)
        os.chdir(home)
        os.execv(NIFT, [NIFT])
    out = b""

    def read_for(seconds):
        nonlocal out
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([fd], [], [], 0.05)
            if not ready:
                continue
            try:
                data = os.read(fd, 4096)
            except OSError:
                return
            if not data:
                return
            out += data

    read_for(0.8)
    for line, delay in lines:
        os.write(fd, line)
        read_for(delay)
    return pid, fd, out, home


def finish(pid, fd, home):
    try:
        os.write(fd, b"exit\r")
    except OSError:
        pass
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    try:
        os.close(fd)
    except OSError:
        pass
    shutil.rmtree(home, ignore_errors=True)


def pty_sigint():
    pid, fd, out, home = pty_session([(b"/bin/sleep 5\r", 0.4), (b"\x03", 0.5),
                                      (b"/bin/echo ALIVE-$((1))\r".replace(b"$((1))", b"MARK"), 0.8)])
    alive = os.waitpid(pid, os.WNOHANG)[0] == 0
    text = out.decode(errors="replace").replace("\r", "")
    check("pty-sigint-shell-survives", alive, text[-200:])
    check("pty-sigint-next-command", "ALIVE-MARK" in text, text[-200:])
    # The job list records the job-control status used by pre-CP20.
    pid2, fd2, out2, home2 = pty_session([(b"/bin/sleep 5\r", 0.4), (b"\x03", 0.5),
                                          (b"jobs\r", 0.6)])
    text2 = out2.decode(errors="replace").replace("\r", "")
    check("pty-sigint-status-130", "Done (130)" in text2, text2[-200:])
    finish(pid2, fd2, home2)
    finish(pid, fd, home)


def pty_tstp():
    pid, fd, out, home = pty_session([(b"/bin/sleep 3\r", 0.4), (b"\x1a", 0.4),
                                      (b"jobs\r", 0.6), (b"fg\r", 0.3), (b"\x03", 0.4),
                                      (b"/bin/echo RESUME-MARK\r", 0.8)])
    text = out.decode(errors="replace").replace("\r", "")
    check("pty-tstp-stopped", "[1] Stopped" in text, text[-300:])
    check("pty-tstp-fg-resumed", "RESUME-MARK" in text, text[-300:])
    finish(pid, fd, home)


def pty_bg():
    pid, fd, out, home = pty_session([(b"/bin/sleep 2 &\r", 0.3),
                                      (b"/bin/echo FG-RAN\r", 1.2)])
    text = out.decode(errors="replace").replace("\r", "")
    check("pty-bg-foreground-runs", "FG-RAN" in text, text[-300:])
    st = Path(f"/proc/{pid}/stat").read_text().split(")")[1].split()
    check("pty-bg-shell-owns-terminal", st[2] == st[5], f"pgrp={st[2]} tpgid={st[5]}")
    os.write(fd, b"wait\r")
    extra = b""
    end = time.time() + 3
    while time.time() < end:
        ready, _, _ = select.select([fd], [], [], 0.1)
        if ready:
            try: extra += os.read(fd, 4096)
            except OSError: break
    os.write(fd, b"jobs\r")
    time.sleep(0.5)
    try: extra += os.read(fd, 4096)
    except OSError: pass
    text2 = (text + extra.decode(errors="replace")).replace("\r", "")
    check("pty-bg-wait-reaps", "Done (0)" in text2, text2[-300:])
    finish(pid, fd, home)


def pty_host_bg_never():
    # Interactive jobs now use the same Host table as scripts; H1 must still
    # prevent a background job from taking the controlling terminal.
    pid, fd, out, home = pty_session([(b"/bin/sleep 2 &\r", 0.3),
                                      (b"/bin/echo HOST-FG\r", 1.2)])
    text = out.decode(errors="replace").replace("\r", "")
    st = Path(f"/proc/{pid}/stat").read_text().split(")")[1].split()
    check("pty-host-bg-never", "HOST-FG" in text and st[2] == st[5],
          f"out={text[-200:]!r} pgrp={st[2]} tpgid={st[5]}")
    finish(pid, fd, home)


def pty_f4_jobs():
    pid, fd, out, home = pty_session([(b"/bin/sleep 0.6 &\r", 0.3), (b"jobs\r", 0.5)])
    text = out.decode(errors="replace").replace("\r", "")
    check("pty-f4-one-job-id", text.count("[1] Running") == 1, text[-300:])
    os.write(fd, b"wait 1\r")
    time.sleep(0.9)
    os.write(fd, b"jobs\r")
    time.sleep(0.5)
    try: extra = os.read(fd, 4096).decode(errors="replace").replace("\r", "")
    except OSError: extra = ""
    check("pty-f4-wait-identity", "unknown job" not in extra and "Done (0)" in extra, extra[-300:])
    finish(pid, fd, home)

    pid2, fd2, out2, home2 = pty_session([(b"/bin/sleep 0.5 &\r", 0.3), (b"fg\r", 1.0)])
    text2 = out2.decode(errors="replace").replace("\r", "")
    check("pty-f4-fg-current", "no current job" not in text2, text2[-300:])
    finish(pid2, fd2, home2)


def zombie_check():
    script = b"/bin/true &\nwait\n/bin/sleep 0.5\nexit\n"
    proc = subprocess.Popen([NIFT], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, cwd="/tmp")
    proc.stdin.write(script); proc.stdin.close()
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


def fd_limits():
    def limit(n):
        def apply():
            resource.setrlimit(resource.RLIMIT_NOFILE, (n, n))
        return apply
    cases = [
        ("fd-3-stage-8", 8, b"/bin/echo hi | /bin/cat | /bin/cat\nexit\n", b"hi\n"),
        ("fd-8-stage-10", 10, b"/bin/echo hi | /bin/cat | /bin/cat | /bin/cat | /bin/cat | /bin/cat | /bin/cat | /bin/cat\nexit\n", b"hi\n"),
    ]
    for name, n, script, expected in cases:
        proc = subprocess.run([NIFT], input=script, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              cwd="/tmp", preexec_fn=limit(n), timeout=20)
        check(name, proc.returncode == 0 and proc.stdout == expected,
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")


def leftover():
    with tempfile.TemporaryDirectory(prefix="cp20-left-") as tmp:
        root = Path(tmp)
        script = (b"/bin/sh -c 'sleep 0.4; touch bg-marker' &\nwait\n"
                  b"/bin/sh -c 'test -f bg-marker && echo WAITED || echo EARLY'\n")
        proc = subprocess.run([NIFT], input=script, cwd=root, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=20)
        check("background-reaped", b"WAITED" in proc.stdout, proc.stdout + proc.stderr)


def fs_root():
    with tempfile.TemporaryDirectory(prefix="cp20-root-") as tmp:
        base = Path(tmp)
        root = base / "root"
        root.mkdir()
        outside = base / "outside.txt"
        env = dict(os.environ, NIFT_FS_ROOT=str(root))
        script = f"/bin/echo OUT > {outside}\nexit\n".encode()
        proc = subprocess.run([NIFT], input=script, cwd=base, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        check("fs-root-redirection-legacy", outside.exists() and outside.read_text().strip() == "OUT",
              f"outside={outside.exists()} stderr={proc.stderr!r}")


def io_messages():
    with tempfile.TemporaryDirectory(prefix="cp20-io-") as tmp:
        root = Path(tmp)
        for name, script, needle in (
            ("stdin-open-message", "/bin/cat < missing-input.txt\n", "cannot open stdin"),
            ("stdout-open-message", "/bin/echo x > missing-dir/x\n", "cannot open stdout"),
        ):
            proc = subprocess.run([NIFT], input=(script + "exit\n").encode(), cwd=root,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
            text = (proc.stdout + proc.stderr).decode(errors="replace")
            check(name, needle in text, text)


def fd_leak_pty():
    pid, fd, out, home = pty_session([])
    before = len(os.listdir(f"/proc/{pid}/fd"))
    os.write(fd, b"/bin/echo hi | /bin/cat | /bin/cat\r")
    time.sleep(0.8)
    after = len(os.listdir(f"/proc/{pid}/fd"))
    check("fd-no-leak", after <= before, f"before={before} after={after}")
    finish(pid, fd, home)


def missing_interpreter():
    with tempfile.TemporaryDirectory(prefix="cp20-interp-") as tmp:
        path = Path(tmp) / "bad-script"
        path.write_text("#!/no/such/interpreter\n")
        path.chmod(0o755)
        proc = subprocess.run([NIFT], input=(str(path) + "\nexit\n").encode(), cwd=tmp,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        text = (proc.stdout + proc.stderr).decode(errors="replace")
        check("missing-interpreter-named", "/no/such/interpreter" in text, text)


def main():
    pty_sigint()
    pty_tstp()
    pty_bg()
    pty_host_bg_never()
    pty_f4_jobs()
    zombie_check()
    fd_limits()
    leftover()
    fs_root()
    io_messages()
    fd_leak_pty()
    missing_interpreter()
    print(f"PASS {22 - len(failures)}, FAIL {len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
