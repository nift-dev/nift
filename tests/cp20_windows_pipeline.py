#!/usr/bin/env python3
"""CP20 R2 Windows CI probe (POSIX hosts skip).

F1: an external command in the piped (scripted) shell must still run on the
legacy Windows launcher after CP21 removed the forced-interactive switch.
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

    external = run('cmd /c echo hello\nexit\n')
    if external.stdout.replace(b"\r\n", b"\n") != b"hello\n":
        ok = False
        print("FAIL piped-external", external.stdout, external.stderr)
    else:
        print("PASS piped-external")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
