#!/usr/bin/env python3
"""Deterministic interpreter allocation slopes for captures and call dispatch.

Requires Valgrind. Budgets are calibrated on Linux x86_64/libstdc++; this is an
explicit performance target, not a wall-clock test or a public language contract.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

binary = str(Path(os.environ.get("NIFT", "./nift")).resolve())
if not shutil.which("valgrind"):
    raise SystemExit("allocation guard requires Valgrind")
counts = {}
with tempfile.TemporaryDirectory(prefix="nift-v410-alloc-") as directory:
    root = Path(directory)
    for n in (100, 200, 400):
        cases = {}
        for extra in (0, 16):
            declarations = "".join(f"unused{i} := {i}\n" for i in range(extra))
            cases[f"capture-{extra}"] = (
                declarations + f"a := []\ni := 1\n"
                f"while(i <= {n}) {{ a.push({n}-i+1); i += 1 }}\n"
                f"a = a.sort_by(x => x)\nprint(a[0]); print(a[{n}-1])\n",
                f"1\n{n}\n")
        cases["dispatch"] = (
            f"offset := 1\nf := x => x + offset\ns := 0\ni := 0\n"
            f"while(i < {n}) {{ s += f(i); i += 1 }}\nprint(s)\n",
            f"{n*(n+1)//2}\n")
        for name, (source, expected) in cases.items():
            path = root / f"{name}-{n}.f"
            path.write_text(source)
            result = subprocess.run(
                ["valgrind", "--error-exitcode=97", "--leak-check=full",
                 binary, str(path)], capture_output=True, text=True, timeout=120)
            if result.returncode or result.stdout != expected:
                raise SystemExit(f"FAIL {name}/{n}: {result.stdout} {result.stderr}")
            if "ERROR SUMMARY: 0 errors" not in result.stderr:
                raise SystemExit(f"FAIL {name}/{n}: {result.stderr}")
            match = re.search(r"total heap usage: ([\d,]+) allocs", result.stderr)
            if not match:
                raise SystemExit("Valgrind allocation summary missing")
            counts[name, n] = int(match[1].replace(",", ""))

results = []
for low, high in ((100, 200), (200, 400)):
    additional_captures = (
        counts["capture-16", high] - counts["capture-0", high]
        - counts["capture-16", low] + counts["capture-0", low])
    capture_slope = additional_captures / (16 * (high - low))
    dispatch_slope = (counts["dispatch", high] - counts["dispatch", low]) / (high - low)
    results.append(dict(low=low, high=high, capture_slope=capture_slope,
                        dispatch_slope=dispatch_slope))
print(json.dumps(results, indent=2))
failures = []
for row in results:
    if row["capture_slope"] > 2.5:
        failures.append(f"capture allocations/entry={row['capture_slope']:.2f} > 2.5")
    if row["dispatch_slope"] > 36:
        failures.append(f"dispatch allocations/call={row['dispatch_slope']:.2f} > 36")
if failures:
    raise SystemExit("FAIL allocation guard: " + "; ".join(failures))
print("PASS capture and native-dispatch allocation slopes (N/2N/4N)")
