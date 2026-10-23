#!/usr/bin/env python3
import os, subprocess, sys
from pathlib import Path
real=os.environ["NIFT_REAL"]; state=Path(".fake-crashed")
if os.environ.get("NIFT_TEST_CRASH_AT"):
    state.write_text("1"); os.execve(real,[real,*sys.argv[1:]],dict(os.environ))
if state.exists():
    state.unlink(); rc=subprocess.call([real,*sys.argv[1:]])
    if rc==0 and len(sys.argv)>1 and sys.argv[1].startswith("build"):
        p=Path("public/index.html")
        if p.exists(): p.write_bytes(p.read_bytes()+b"BAD-ORACLE\n")
    sys.exit(rc)
os.execve(real,[real,*sys.argv[1:]],dict(os.environ))
