#!/usr/bin/env python3
"""CP22 evidence probe (not a gate): a thread blocked in a Host op that does
not suspend (`open` of a FIFO whose writer is an external process that writes
after DELAY seconds) while a second thread only reads the clock.

  blocking_probe.py --nift ./nift [--delay 2]

Legacy runs each thread() on its own OS thread: the second thread finishes at
once. The VM (thread() only with NIFT_VM_COOPERATIVE_THREADS=1) runs tasks on
one thread: the blocked task holds it, so the second thread finishes only after
the external write (`epoch()` is in milliseconds; starvation for the duration of the block; a deadlock if
the writer depended on another VM thread). Prints one JSON line per engine.
"""
import argparse
import json
import os
import subprocess
import tempfile
import threading
import time
from pathlib import Path

PROGRAM = """fn(reader()) { return open("p") }
fn(stamp()) { return epoch() }
t0 := epoch()
a := thread(reader)
b := thread(stamp)
print(b.join() - t0)
print(a.join())
"""


def run(nift, engine, delay):
    with tempfile.TemporaryDirectory(prefix="cp22-block-") as d:
        Path(d, "probe.f").write_text(PROGRAM)
        os.mkfifo(Path(d, "p"))
        env = dict(os.environ, NIFT_VM="0" if engine == "legacy" else "require")
        if engine == "vm":
            env.update({"NIFT_VM_COOPERATIVE_THREADS": "1", "NIFT_VM_STATS": str(Path(d, "stats.jsonl"))})

        def writer():
            time.sleep(delay)
            with open(Path(d, "p"), "w") as fifo:
                fifo.write("hello")

        w = threading.Thread(target=writer)
        start = time.perf_counter()
        w.start()
        p = subprocess.run([nift, "probe.f"], cwd=d, env=env, capture_output=True, timeout=60)
        wall = time.perf_counter() - start
        w.join()
        stats = Path(d, "stats.jsonl")
        outcome = json.loads(stats.read_text().splitlines()[-1]).get("outcome") if stats.exists() else None
        lines = p.stdout.decode().split("\n")
        return {"engine": engine, "exit": p.returncode, "second_thread_done_after_ms": lines[0],
                "reader_result": lines[1] if len(lines) > 1 else None, "wall_s": round(wall, 3),
                "vm_outcome": outcome, "writer_delay_s": delay}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--delay", type=float, default=2.0)
    a = ap.parse_args()
    for engine in ("legacy", "vm"):
        print(json.dumps(run(os.path.abspath(a.nift), engine, a.delay), sort_keys=True))


if __name__ == "__main__":
    main()
