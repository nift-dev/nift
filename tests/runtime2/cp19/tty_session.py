"""Run nift on a real terminal (pty) for the typed-REPL contract (owner ruling,
Option A: a tty REPL continues after a refusal; piped input stops with exit 1).
run_tty returns (exit status, output); the pty merges stdout and stderr, CRLF
becomes LF and the line editor's prompt redraws are dropped, so the output is
the statements' output and diagnostics in order."""
import os
import pty
import select
import signal
import time


def run_tty(argv, cwd, env, lines, settle=0.3, timeout=30):
    env = dict(env, TERM="dumb")
    pid, fd = pty.fork()
    if pid == 0:
        try:
            os.chdir(cwd)
            os.execve(argv[0], argv, env)
        finally:
            os._exit(127)
    out = b""

    def drain(seconds):
        nonlocal out
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([fd], [], [], 0.05)
            if not ready:
                continue
            try:
                data = os.read(fd, 65536)
            except OSError:
                return False
            if not data:
                return False
            out += data
        return True
    for line in lines:
        drain(settle)
        os.write(fd, (line + "\r").encode())
    deadline = time.time() + timeout
    status = None
    while status is None:
        drain(0.2)
        done, raw = os.waitpid(pid, os.WNOHANG)
        if done:
            status = raw
            drain(0.2)
        elif time.time() > deadline:
            os.kill(pid, signal.SIGKILL)
            _, status = os.waitpid(pid, 0)
    os.close(fd)
    text = out.decode(errors="replace").replace("\r\n", "\n")
    kept = [chunk.split("\r")[-1] for chunk in text.split("\n")]
    return os.waitstatus_to_exitcode(status), "\n".join(k for k in kept if not k.startswith("\x1b[2K"))
