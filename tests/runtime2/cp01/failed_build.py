#!/usr/bin/env python3
import os, subprocess, sys
r = subprocess.run([os.environ["NIFT_REAL"], *sys.argv[1:]])
if sys.argv[1:2] == ["build"] and r.returncode == 0:
    sys.exit(1)
sys.exit(r.returncode)
