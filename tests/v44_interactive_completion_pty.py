#!/usr/bin/env python3
"""PTY regression for the interactive nift sh line editor.

Drives the shell through a pseudo-terminal and verifies TAB completion
(command, builtin, file path), history recall (up-arrow), Ctrl-C interrupt
(clears the line without killing the shell) and Ctrl-D EOF. Uses only the
public executable interface, so it doubles as an independent contract.
"""
import os, pty, select, subprocess, sys, time

# The interactive raw-mode line editor is Unix-only; Windows `nift sh` uses
# line-based input until the platform backend lands. Skip there.
if os.name == "nt":
    print("SKIP v4.4 interactive completion PTY (Windows uses line-based input)")
    sys.exit(77)

NIFT = sys.argv[1] if len(sys.argv) > 1 else "./nift"
FAILS = []


def check(name, cond):
    print(f"{'PASS' if cond else 'FAIL'} {name}")
    if not cond:
        FAILS.append(name)


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


def send(fd, s):
    os.write(fd, s.encode())


pid, fd = pty.fork()
WORK = os.path.join(os.path.dirname(os.path.abspath(NIFT)), ".interactive-completion-test")
os.makedirs(WORK, exist_ok=True)
if pid == 0:
    os.chdir(WORK)
    os.execv(NIFT, [NIFT, "sh"])

try:
    drain(fd, 0.4)
    # 1) TAB completes a PATH command: ech -> echo
    send(fd, "ech\t")
    time.sleep(0.3)
    tab1 = drain(fd)
    check("tab-completes-command", "echo" in tab1)
    # 2) run the completed command with an argument
    send(fd, " hello pty\n")
    time.sleep(0.6)
    out2 = drain(fd)
    check("completed-command-runs", "hello pty" in out2)
    # 3) up-arrow recalls the previous line from history
    send(fd, "\x1b[A\n")
    time.sleep(0.8)
    out3 = drain(fd)
    check("history-up-arrow", "hello pty" in out3)
    # 4) TAB completes a Nift builtin: pri -> print
    send(fd, "pri\t")
    time.sleep(0.3)
    out4 = drain(fd)
    check("tab-completes-builtin", "print" in out4)
    # 5) file-path completion in a subdirectory
    os.makedirs(os.path.join(WORK, "dir_complete_xyz"), exist_ok=True)
    send(fd, "ls dir_compl\t")
    time.sleep(0.3)
    out5 = drain(fd)
    check("tab-completes-path", "dir_complete_xyz" in out5)
    send(fd, "ete_xyz\n")
    time.sleep(0.5)
    drain(fd)
    # 6) Ctrl-C clears a half-typed line without killing the shell
    send(fd, "echo half")
    time.sleep(0.2)
    send(fd, "\x03")
    time.sleep(0.4)
    drain(fd)
    alive = os.waitpid(pid, os.WNOHANG)
    check("ctrl-c-keeps-shell", alive == (0, 0))
    send(fd, "echo after_int\n")
    time.sleep(0.6)
    out6 = drain(fd)
    check("ctrl-c-then-command", "after_int" in out6)
    # 7) user-defined function participates in completion
    send(fd, "greet := (n) => 'hi ' + n\n")
    time.sleep(0.4)
    drain(fd)
    send(fd, "gre\t")
    time.sleep(0.3)
    out7 = drain(fd)
    check("tab-completes-user-function", "greet" in out7)
    send(fd, "et('bob')\n")
    time.sleep(0.5)
    out8 = drain(fd)
    check("user-function-runs", "hi bob" in out8)
    # 8) Ctrl-D on an empty line is EOF
    send(fd, "\x04")
    time.sleep(0.5)
    drain(fd)
    try:
        deadline = time.time() + 5.0
        while time.time() < deadline:
            got, _ = os.waitpid(pid, os.WNOHANG)
            if got == pid:
                break
            time.sleep(0.05)
        else:
            os.kill(pid, 9)
            check("ctrl-d-exits", False)
            raise RuntimeError("shell did not exit on Ctrl-D")
        check("ctrl-d-exits", True)
    except ChildProcessError:
        check("ctrl-d-exits", False)
finally:
    try:
        os.close(fd)
    except OSError:
        pass

if FAILS:
    print("FAILED:", ", ".join(FAILS))
    sys.exit(1)
print("PASS v4.4 interactive completion PTY")
