#!/usr/bin/env python3
"""CP20 R1 interactive benchmark: 200 external commands through a real PTY.
This path is J1(a)'s legacy interactive foreground path; scripted results are
reported separately. usage: cp20_pty_bench.py NIFT [rounds] [commands]."""
import os
import pty
import select
import statistics
import sys
import tempfile
import time

nift = os.path.abspath(sys.argv[1])
rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 10
commands = int(sys.argv[3]) if len(sys.argv) > 3 else 200
samples = []
for _ in range(rounds):
    home = tempfile.mkdtemp(prefix="cp20-ptybench-")
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["HOME"] = home
        os.environ["TERM"] = "dumb"
        os.chdir(home)
        os.execv(nift, [nift])
    # Wait for the prompt, then send the batch as fast as the pty accepts it.
    select.select([fd], [], [], 1.0)
    os.read(fd, 4096)
    start = time.perf_counter()
    payload = (("/usr/bin/true\r") * commands + "exit\r").encode()
    os.write(fd, payload)
    while True:
        ready, _, _ = select.select([fd], [], [], 0.1)
        if ready:
            try:
                if not os.read(fd, 4096):
                    break
            except OSError:
                break
        done, _ = os.waitpid(pid, os.WNOHANG)
        if done == pid:
            break
    samples.append((time.perf_counter() - start) * 1000.0 / commands)
    try:
        os.close(fd)
    except OSError:
        pass
    import shutil
    shutil.rmtree(home, ignore_errors=True)
print("interactive-pty: median=%.4f ms/launch p25=%.4f p75=%.4f samples=%s" % (
    statistics.median(samples),
    statistics.quantiles(samples, n=4)[0],
    statistics.quantiles(samples, n=4)[2],
    [round(x, 3) for x in samples]))
