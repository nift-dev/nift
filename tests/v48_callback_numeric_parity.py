#!/usr/bin/env python3
"""Compare cached lambda arithmetic to ordinary legacy expressions, including errors."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

binary = str(Path(os.environ.get("NIFT", "./nift")).resolve())
expressions = ["a + b", "a-b", "a * b", "a / b", "a % b", "-a", "+b",
               "a + b * c", "(a+b)*c", "a-b-c", "a/(b+c)", "-(a+b)",
               "a + (b % c)", "a + 9007199254740993", "a + 9223372036854775807",
               "a - -9223372036854775808", "a * 1e100", "a + 0x10", "a + 0Xff", "a + 0x1e",
               "a + 01", "a + 1.", "a + .5", "a + 1e+2", "a + 1e-3", "a < b", "a <= b", "a > b", "a >= b", "a+b < c", "a < b+c",
               "a < b < c", "a > b < c", "(a<b)+c", "a == b", "a != b", "a % b == c", "a+b != c", "a == b == c", "a ?? b", "a > b && b > c",
               "++a", "--a", "a++", "a--", "a+++b", "a--b"]
values = [("2", "3", "4"), ("-7", "2", "3"), ("1.25", "2.5", "0.5"),
          ("9007199254740993", "2", "3"), ("9223372036854775807", "1", "2"),
          ("-9223372036854775808", "-1", "2"), ("2", "0", "0"),
          ('"a"', '"b"', '"c"'), ("[1]", "[2]", "[3]"), ("null", "2", "3")]


def message(stderr):
    # Source locations and the existing lambda context prefix differ by route.
    return re.sub(r"^error: .*?:\d+:\d+: (?:lambda body error: )?", "", stderr).strip()


with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "p.f"
    count = 0
    for expression in expressions:
        for a, b, c in values:
            prefix = f"a := {a}\nb := {b}\nc := {c}\n"
            results = []
            for suffix in (f"print({expression})\n",
                           f"f := (a,b,c) => {expression}\nprint(f(a,b,c))\n"):
                path.write_text(prefix + suffix)
                result = subprocess.run([binary, str(path)], capture_output=True, text=True)
                results.append(result)
            ordinary, callback = results
            if (ordinary.returncode == 0) != (callback.returncode == 0):
                raise SystemExit(f"FAIL status parity: {expression}, {(a,b,c)}, {results}")
            if ordinary.returncode == 0:
                if ordinary.stdout != callback.stdout:
                    raise SystemExit(f"FAIL value parity: {expression}, {(a,b,c)}: "
                                     f"{ordinary.stdout!r} != {callback.stdout!r}")
            elif message(ordinary.stderr) != message(callback.stderr):
                raise SystemExit(f"FAIL error parity: {expression}, {(a,b,c)}: "
                                 f"{ordinary.stderr!r} != {callback.stderr!r}")
            count += 1
print(f"PASS {count} ordinary/callback numeric and fallback parity pairs")
