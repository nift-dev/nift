#!/usr/bin/env python3
"""B2 failure controls: a post-crash wrapper can fail the plain recovery or
corrupt its output while still returning success."""
import os
import sys
from pathlib import Path
real = os.environ["NIFT_REAL"]
state = Path(".fake-crashed")
if os.environ.get("NIFT_TEST_CRASH_AT"):
    state.write_text("1")
    os.execve(real, [real, *sys.argv[1:]], dict(os.environ))
if state.exists():
    mode = os.environ.get("NIFT_FAKE_CRASH_MODE", "")
    state.unlink()
    if mode == "fail":
        sys.exit(1)
    if mode == "bad-oracle":
        os.execve(real, [real, *sys.argv[1:]], dict(os.environ))
        # not reached
os.execve(real, [real, *sys.argv[1:]], dict(os.environ))
