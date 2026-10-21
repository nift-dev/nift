#!/usr/bin/env python3
"""CP19 N1: line-start completion categories.

Command position offers shell-state commands, print/pwd, user functions and
PATH executables. Nift built-ins outside the allow-list are offered in call
form only (exists(, open(, ...), never as bare command words.
"""
import os
import pty
import select
import signal
import sys
import tempfile
import time
from pathlib import Path

if os.name != "posix":
    print("SKIP CP19 completion (POSIX PTY test)")
    sys.exit(77)

NIFT = str(Path(sys.argv[1] if len(sys.argv) > 1 else os.environ.get("NIFT", "./nift")).resolve())
WORK = Path(tempfile.mkdtemp(prefix="nift-cp19-completion-"))
BIN = WORK / "bin"
BIN.mkdir()
tool = BIN / "fixtool"
tool.write_text("#!/bin/sh\necho stub-fixtool\n", encoding="utf-8")
tool.chmod(0o755)

env = os.environ.copy()
env["HOME"] = str(WORK)
env["PATH"] = str(BIN) + ":" + env.get("PATH", "/usr/bin:/bin")

fails = []


def check(name, condition, detail=""):
    if condition:
        print(f"PASS {name}")
    else:
        print(f"FAIL {name}" + (f": {detail}" if detail else ""))
        fails.append(name)


def drain(fd, timeout=0.8):
    out = b""
    end = time.time() + timeout
    while time.time() < end:
        ready, _, _ = select.select([fd], [], [], 0.1)
        if ready:
            try:
                out += os.read(fd, 4096)
            except OSError:
                break
    return out.decode(errors="replace")


def send(fd, text):
    os.write(fd, text.encode())


pid, fd = pty.fork()
if pid == 0:
    os.chdir(WORK)
    os.execve(NIFT, [NIFT], env)

try:
    drain(fd, 0.5)

    # PATH executable.
    send(fd, "fix\t")
    time.sleep(0.3)
    out = drain(fd)
    check("completion PATH command", "fixtool" in out, out)
    send(fd, "\x03")
    time.sleep(0.2)
    drain(fd)

    # Nift built-in in call form (unique prefix).
    send(fd, "exist\t")
    time.sleep(0.3)
    out = drain(fd)
    check("completion built-in call form", "exists(" in out, out)
    send(fd, "\x03")
    time.sleep(0.2)
    drain(fd)

    # Shared call-form prefix must not insert a bare built-in command.
    send(fd, "ope\t")
    time.sleep(0.3)
    out = drain(fd)
    check("completion prefers call form", "open(" in out, out)
    send(fd, "\n")
    time.sleep(0.3)
    out += drain(fd)
    check("completion does not insert bare builtin",
          "... " in out and "nift: open: command not found" not in out and
          "nift: ope: command not found" not in out, out)
    send(fd, "\x03")
    time.sleep(0.2)
    drain(fd)

    # User-defined function.
    send(fd, 'greet := (n) => "hi " + n\n')
    time.sleep(0.3)
    drain(fd)
    send(fd, "gre\t")
    time.sleep(0.3)
    out = drain(fd)
    check("completion user function", "greet" in out, out)
    send(fd, "\x03")
    time.sleep(0.2)
    drain(fd)

    send(fd, "exit\n")
    time.sleep(0.4)
    try:
        deadline = time.time() + 5.0
        while time.time() < deadline:
            got, _ = os.waitpid(pid, os.WNOHANG)
            if got == pid:
                break
            time.sleep(0.05)
        else:
            os.kill(pid, signal.SIGKILL)
            try:
                os.waitpid(pid, 0)
            except OSError:
                pass
    except ChildProcessError:
        pass
finally:
    try:
        os.close(fd)
    except OSError:
        pass

if fails:
    print("FAILED: " + ", ".join(fails))
    sys.exit(1)
print("PASS CP19 completion categories")
