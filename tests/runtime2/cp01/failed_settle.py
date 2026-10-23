#!/usr/bin/env python3
import os, signal, subprocess, sys
r = subprocess.run([os.environ["NIFT_REAL"], *sys.argv[1:]], capture_output=True)
sys.stdout.buffer.write(r.stdout); sys.stderr.buffer.write(r.stderr)
sys.stdout.flush(); sys.stderr.flush()
if r.returncode < 0:
    os.kill(os.getpid(), -r.returncode)
if sys.argv[1:2] == ["build"] and r.returncode == 0 and b"up to date" in r.stdout:
    sys.exit(1)
sys.exit(r.returncode)
