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

FIXTURE = os.path.join(T, ".interactive-foreground-probe")
with open(FIXTURE, "w") as f:
    f.write("#!/bin/sh\nfor s in 0 1 2; do if [ -t $s ]; then echo fd$s-tty; else echo fd$s-pipe; fi; done\n")
os.chmod(FIXTURE, 0o755)

FAILS = []

def check(name, cond, detail=""):
    print(f"{'PASS' if cond else 'FAIL'} {name}")
    if not cond:
        FAILS.append(name + (" " + detail if detail else ""))

def run_in_shell(cmd, timeout=3.0):
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(T)
        os.execv(NIFT, [NIFT, "sh"])
    time.sleep(0.4)
    os.write(fd, cmd.encode())
    time.sleep(timeout)
    out = b""
    while True:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            break
        try:
            out += os.read(fd, 4096)
        except OSError:
            break
    os.write(fd, b"exit\n")
    time.sleep(0.2)
    try:
        os.close(fd)
        os.waitpid(pid, 0)
    except (OSError, ChildProcessError):
        pass
    return out.decode(errors="replace")

# Foreground simple command: all three streams must be TTYs (direct inherit).
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
out = run_in_shell(f"{FIXTURE} > {redir}\ncat {redir}\nexit\n")
check("redirection-routes-stdout", "fd1-pipe" in out, out)

# Structured run() must capture, not attach the terminal.
probe_script = os.path.join(T, ".interactive-run-probe.f")
with open(probe_script, "w") as f:
    f.write('r := run("' + FIXTURE + '")\nprint(r.stdout.trim())\n')
out = subprocess.run([NIFT, "run", probe_script], capture_output=True, text=True).stdout
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
