#!/usr/bin/env python3
"""CP20 R2 Windows CI probe (POSIX hosts skip).

F1: with the interactive branch forced, an external command must still run
(F1 regression on R1).
F2: a console pipeline must expose the pre-CP20 captured/normalised LF bytes;
the original CP20 `(false,false)` path emits raw CRLF.
"""
import os
import subprocess
import sys
from pathlib import Path

if os.name != "nt":
    print("SKIP CP20 Windows pipeline (POSIX host)")
    sys.exit(77)

nift = str(Path(sys.argv[1] if len(sys.argv) > 1 else "./nift").resolve())


def run(script, extra_env=None):
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    return subprocess.run([nift], input=script.encode(), env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)


def main():
    ok = True
    pipeline = run('cmd /c "echo one" | findstr one\nexit\n')
    if pipeline.stdout != b"one\n":
        ok = False
        print("FAIL console-pipeline-bytes", pipeline.stdout)
    else:
        print("PASS console-pipeline-bytes")

    redirected = run('cmd /c "echo two" 2> err.txt\nexit\n')
    if redirected.stdout != b"two\n":
        ok = False
        print("FAIL redirected-console-bytes", redirected.stdout)
    else:
        print("PASS redirected-console-bytes")

    interactive = run('cmd /c echo hello\nexit\n', {"NIFT_CP20_FORCE_INTERACTIVE": "1"})
    if interactive.stdout.replace(b"\r\n", b"\n") != b"hello\n":
        ok = False
        print("FAIL forced-interactive-external", interactive.stdout, interactive.stderr)
    else:
        print("PASS forced-interactive-external")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
