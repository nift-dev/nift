#!/usr/bin/env python3
"""Relative guard against generic classification of every identity callback body."""
import os
from pathlib import Path
import statistics
import subprocess
import tempfile
import time

binary = str(Path(os.environ.get("NIFT", "./nift")).resolve())
build = "a := []\ni := 1\nwhile(i <= 100000) { a.push(i); i += 1 }\n"
programs = {
    "build": build + "print(a.size())\n",
    "identity": build + "a = a.map(x => x)\nprint(a.size())\n",
    "filter": build + "a = a.filter(x => true)\nprint(a.size())\n",
}
samples = {name: [] for name in programs}
with tempfile.TemporaryDirectory() as directory:
    paths = {}
    for name, source in programs.items():
        path = Path(directory) / (name + ".f")
        path.write_text(source)
        paths[name] = path
    # Alternate ordering to reduce systematic drift; startup belongs to every run.
    for iteration in range(5):
        order = list(programs)
        if iteration % 2:
            order.reverse()
        for name in order:
            start = time.perf_counter()
            result = subprocess.run([binary, str(paths[name])], check=True,
                                    capture_output=True, text=True)
            samples[name].append(time.perf_counter() - start)
            assert result.stdout == "100000\n", (name, result.stdout)
medians = {name: statistics.median(values) for name, values in samples.items()}
for name in ("identity", "filter"):
    ratio = medians[name] / medians["build"]
    if ratio > 4:
        raise SystemExit(f"FAIL identifier dispatch guard: {name}/build={ratio:.2f} > 4")
print("PASS identifier dispatch ratios: " + ", ".join(
    f"{name}/build={medians[name] / medians['build']:.2f}"
    for name in ("identity", "filter")))
