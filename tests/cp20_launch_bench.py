#!/usr/bin/env python3
"""CP20 local launch-cost probe.

200 external `/usr/bin/true` launches (single and three-stage) per batch, five
batches, median wall-clock per launch. The shell script executes the real
external binary, not a shell built-in. This is the local probe; certification
uses the g6-dedicated-4 Linode type.
"""
import argparse
import os
import statistics
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--nift", default="./nift")
parser.add_argument("--runs", type=int, default=5)
parser.add_argument("--commands", type=int, default=200)
args = parser.parse_args()
nift = str(Path(args.nift).resolve())
true = "/usr/bin/true"
if not os.access(true, os.X_OK):
    raise SystemExit(f"missing external executable {true}")


def batch(cmd, script, repetitions):
    samples = []
    for _ in range(repetitions):
        start = time.perf_counter()
        subprocess.run(cmd, input=script, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       cwd="/tmp", check=False)
        samples.append((time.perf_counter() - start) * 1000.0 / args.commands)
    return statistics.median(samples), samples


for label, command_line in (("single", true), ("pipe3", f"{true} | {true} | {true}")):
    body = ((command_line + "\n") * args.commands + "exit\n").encode()
    bash_body = ((command_line + "\n") * args.commands).encode()
    n, ns = batch([nift], body, args.runs)
    b, bs = batch(["/bin/bash"], bash_body, args.runs)
    print(f"{label}: nift={n:.3f} ms/launch bash={b:.3f} ms/launch "
          f"ratio={n / b:.2f} delta={(n / b - 1) * 100:.1f}%")
    print(f"  nift samples: {[round(x, 3) for x in ns]}")
    print(f"  bash samples: {[round(x, 3) for x in bs]}")
