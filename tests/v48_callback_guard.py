#!/usr/bin/env python3
"""Count preparation versus invocation in the dedicated instrumented CLI."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

binary = str(Path(os.environ.get("NIFT", "./nift")).resolve())
build = "a := []\ni := 1\nwhile(i <= 100000) { a.push(i); i += 1 }\n"
cases = [
    ("indexed map", "a := []\ni := 0\nwhile(i < 100000) { a.push(i); i += 1 }\nprint(a.map(x => a[x]).length())\n",
     "100000\n", {"syntax": 1, "numeric": 1, "instances": 1, "prepared": 100000, "legacy": 0}),
    ("indexed sort", "a := []\ni := 0\nwhile(i < 100000) { a.push(i); i += 1 }\na = a.sort_by(x => a[x])\nprint(a[0])\n",
     "0\n", {"syntax": 1, "numeric": 1, "instances": 100000, "prepared": 100000, "legacy": 0}),
    ("identity map", build + "print(a.map(x => x).length())\n",
     "100000\n", {"syntax": 1, "numeric": 0, "instances": 1, "prepared": 100000, "legacy": 0}),
    ("identity sort", build + "a = a.sort_by(x => x)\nprint(a[0])\n",
     "1\n", {"syntax": 1, "numeric": 0, "instances": 100000, "prepared": 100000, "legacy": 0}),
    ("identity typed fallback", 'f := x => x\nprint(f(2))\nprint(f("a"))\n',
     "2\na\n", {"syntax": 1, "numeric": 0, "instances": 1, "prepared": 1, "legacy": 1}),
    ("sort", build + "a = a.sort_by(x => x + 1)\nprint(a[0])\n",
     "1\n", {"syntax": 1, "numeric": 1, "instances": 100000, "prepared": 100000, "legacy": 0}),
    ("reduce", build + "print(a.reduce((acc,x) => acc + x, 0))\n",
     "5000050000\n", {"syntax": 1, "numeric": 1, "instances": 1, "prepared": 100000, "legacy": 0}),
    ("numeric filter", build + "print(a.filter(x => x > 50000).length())\n",
     "50000\n", {"syntax": 1, "numeric": 1, "instances": 1, "prepared": 100000, "legacy": 0}),
    ("numeric equality filter", build + "print(a.filter(x => x % 2 == 0).length())\n",
     "50000\n", {"syntax": 1, "numeric": 1, "instances": 1, "prepared": 100000, "legacy": 0}),
    ("bounded cache", "s := 0\n" + "".join(
        f"f{i} := x => x + {i}\ns += f{i}(0)\n" for i in range(300)) + "print(s)\n",
     "44850\n", {"syntax": 300, "numeric": 256, "instances": 300, "prepared": 256, "legacy": 44}),
    ("oversized body", "f := x => x + " + " " * 4096 + "1\nprint(f(1))\n",
     "2\n", {"syntax": 1, "numeric": 0, "instances": 1, "prepared": 0, "legacy": 1}),
    ("typed fallback", 'f := x => x + 1\nprint(f(2))\nprint(f("a"))\n',
     "3\na1\n", {"syntax": 1, "numeric": 1, "instances": 1, "prepared": 1, "legacy": 1}),
]
with tempfile.TemporaryDirectory() as directory:
    for name, source, output, expected in cases:
        path = Path(directory) / "p.f"
        path.write_text(source)
        result = subprocess.run([binary, str(path)], check=True, capture_output=True,
                                text=True, env={**os.environ, "NIFT_TEST_LAMBDA_CACHE_STATS": "1"})
        if result.stdout != output:
            raise SystemExit(f"FAIL {name}: output {result.stdout!r}")
        match = re.search(r"^lambda-cache (.+)$", result.stderr, re.MULTILINE)
        if not match:
            raise SystemExit("FAIL missing test-only cache counters")
        counts = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", match[1])}
        if counts != expected:
            raise SystemExit(f"FAIL {name}: {counts} != {expected}")
        print(f"PASS {name}: {counts}")
