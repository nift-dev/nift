#!/usr/bin/env python3
"""PTY regression: a simple foreground command entered in interactive `nift sh`
must inherit the shell's terminal directly (isatty stdin/stdout/stderr) rather
than being captured through pipes and replayed. run()/cmd() structured capture
is unaffected, and redirections/pipelines keep their own descriptor routing.

Uses a deterministic fixture executable (no dependency on fastfetch/top).
"""
import os, pty, select, subprocess, sys, time

# POSIX-only: pty.fork() drives the interactive terminal. Windows `nift sh`
# is line-based; skip there with the repo's acknowledged-skip code.
if os.name == "nt":
    print("SKIP v4.4 shell foreground TTY (POSIX PTY test)")
    sys.exit(77)

NIFT = sys.argv[1] if len(sys.argv) > 1 else "./nift"
T = os.path.dirname(os.path.abspath(NIFT))

# Hard backstop: report where the test is and exit instead of hanging a runner.
_last = "start"
def _mark(m):
    global _last
    _last = m
    print(f"MARK {m}", flush=True)
try:
    import signal
    signal.signal(signal.SIGALRM, lambda *a: (_ for _ in ()).throw(RuntimeError(f"stuck after {_last}")))
    signal.alarm(100)
except Exception:
    pass

FIXTURE = os.path.join(T, ".interactive-foreground-probe")
with open(FIXTURE, "w") as f:
    f.write("#!/bin/sh\nfor s in 0 1 2; do if [ -t $s ]; then echo fd$s-tty; else echo fd$s-pipe; fi; done\n")
os.chmod(FIXTURE, 0o755)

FAILS = []

def check(name, cond, detail=""):
    print(f"{'PASS' if cond else 'FAIL'} {name}")
    if not cond:
        FAILS.append(name + (" " + detail if detail else ""))

def wait_pid_bounded(pid, deadline):
    """Wait for a child to exit within the deadline; otherwise kill it and
    raise, so a regression produces a failure instead of hanging CI forever."""
    while time.time() < deadline:
        try:
            got, st = os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            return
        if got == pid:
            return
        time.sleep(0.05)
    try:
        os.kill(pid, 9)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except (OSError, ChildProcessError):
        pass
    raise RuntimeError("shell did not exit within the deadline")


def drain(fd, timeout=0.8):
    out = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                out += os.read(fd, 4096)
            except OSError:
                break
    return out.decode(errors="replace")


def run_in_shell(cmd, timeout=3.0):
    # Mirrors the interactive completion PTY test (which passes on macOS):
    # blocking writes + select/read drain. Non-blocking pty mode is avoided
    # because select() on a non-blocking pty master can block indefinitely on
    # macOS kernels, which no Python-side alarm can interrupt.
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(T)
        os.execv(NIFT, [NIFT, "sh"])
    time.sleep(0.4)
    drain(fd, 0.4)
    try:
        os.write(fd, cmd.encode())
    except OSError:
        pass
    time.sleep(0.6)
    out = drain(fd, timeout)
    try:
        os.write(fd, b"exit\n")
    except OSError:
        pass
    wait_pid_bounded(pid, time.time() + 5.0)
    try:
        os.close(fd)
    except OSError:
        pass
    return out


# Foreground simple command: all three streams must be TTYs (direct inherit).
_mark("run1")
out = run_in_shell(FIXTURE + "\n")
check("foreground-stdin-tty", "fd0-tty" in out, out)
check("foreground-stdout-tty", "fd1-tty" in out, out)
check("foreground-stderr-tty", "fd2-tty" in out, out)

# A redirection must route stdout to the file (child sees a pipe/file, not TTY).
redir = os.path.join(T, ".interactive-redir-out")
try:
    os.remove(redir)
except OSError:
    pass
_mark("run2")
out = run_in_shell(f"{FIXTURE} > {redir}\ncat {redir}\nexit\n")
check("redirection-routes-stdout", "fd1-pipe" in out, out)

# Structured run() must capture, not attach the terminal.
probe_script = os.path.join(T, ".interactive-run-probe.f")
with open(probe_script, "w") as f:
    f.write('r := run("' + FIXTURE + '")\nprint(r.stdout.trim())\n')
_mark("run3")
out = subprocess.run([NIFT, "run", probe_script], capture_output=True, text=True, timeout=30).stdout
check("run-captures-not-tty", "fd1-pipe" in out and "fd0-pipe" in out, out)

try:
    os.remove(FIXTURE)
    os.remove(redir)
    os.remove(probe_script)
except OSError:
    pass

if FAILS:
    print("FAILED:", ", ".join(FAILS))
    sys.exit(1)
print("PASS v4.4 shell foreground terminal inheritance")
