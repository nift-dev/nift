#!/usr/bin/env python3
"""B2 control: remove the crash injection and delegate to the real binary."""
import os
import sys
env = dict(os.environ)
env.pop("NIFT_TEST_CRASH_AT", None)
os.execve(os.environ["NIFT_REAL"], [os.environ["NIFT_REAL"], *sys.argv[1:]], env)
