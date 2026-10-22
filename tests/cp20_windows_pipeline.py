#!/usr/bin/env python3
"""CP20 Windows CI probe: pipeline/redirection output keeps the legacy CRLF
normalisation. POSIX hosts skip (exit 77)."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

if os.name != "nt":
    print("SKIP CP20 Windows pipeline (POSIX host)")
    sys.exit(77)

nift = str(Path(sys.argv[1] if len(sys.argv) > 1 else "./nift").resolve())
with tempfile.TemporaryDirectory(prefix="cp20-win-") as tmp:
    root = Path(tmp)
    script = (
        'cmd /c "echo one & echo two" | findstr one > out.txt\n'
        'cmd /c "echo three" >> out.txt\n'
        'type out.txt\n'
        "exit\n"
    )
    proc = subprocess.run([nift], input=script.encode(), cwd=root,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    out = proc.stdout.replace(b"\r\n", b"\n")
    data = (root / "out.txt").read_bytes().replace(b"\r\n", b"\n")
    ok = proc.returncode == 0 and b"one" in out and b"three" in out and b"one\nthree\n" in data
    print(("PASS" if ok else "FAIL") + " cp20-windows-pipeline")
    if not ok:
        print("stdout:", proc.stdout)
        print("stderr:", proc.stderr)
        print("file:", data)
        sys.exit(1)
