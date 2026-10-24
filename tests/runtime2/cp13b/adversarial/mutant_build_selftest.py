#!/usr/bin/env python3
"""B6: isolated mutant builds, distinct binaries and no stale reuse."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent / "mutant_build.py"


def main():
    failures = 0
    with tempfile.TemporaryDirectory(prefix="cp13b-mb-") as folder:
        folder = Path(folder)
        root = folder / "tree"
        root.mkdir()
        (root / "nift").write_text("STALE BINARY\n")  # must not be reused
        (root / "stale.o").write_text("stale\n")
        (root / "Makefile").write_text(
            "nift:\n"
            "\t@printf '#!/bin/sh\\necho \"%s\"\\n' \"$(CXXFLAGS)\" > nift\n"
            "\t@chmod +x nift\n")
        def run(*args):
            return subprocess.run([sys.executable, str(BUILD), "--root", str(root)] + list(args),
                                  capture_output=True, text=True)
        out1, out2 = folder / "out1", folder / "out2"
        r1 = run("--mutant", "1", "--output", str(out1), "--expect-marker=-DNIFT_EXC_MUTANT=1")
        r2 = run("--mutant", "2", "--output", str(out2), "--expect-marker=-DNIFT_EXC_MUTANT=2")
        dry = run("--dry-run")
        for label, result in (("mutant1", r1), ("mutant2", r2), ("dry", dry)):
            if result.returncode != 0:
                print(f"FAIL {label}: rc={result.returncode}\n{result.stdout[-1000:]}{result.stderr[-1000:]}")
                failures += 1
        if failures == 0:
            a, b = out1.read_text(), out2.read_text()
            if a == b:
                print("FAIL mutant binaries are identical")
                failures += 1
            if "-DNIFT_EXC_MUTANT=1" not in a or "-DNIFT_EXC_MUTANT=2" not in b:
                print("FAIL mutant define is not compiled into the binary")
                failures += 1
            if "STALE BINARY" in a or "STALE BINARY" in b:
                print("FAIL stale binary was reused")
                failures += 1
            if "NIFT_EXC_MUTANT" in dry.stdout:
                print("FAIL dry run carried a mutant define")
                failures += 1
    print(f"{'PASS' if failures == 0 else 'FAIL'} isolated mutant build self-test: "
          f"{'clean builds differ and do not reuse stale products' if failures == 0 else failures} ")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
