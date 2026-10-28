#!/usr/bin/env python3
"""CP22 harness negative controls: the gate must fail, for the right reason,
on (1) a dead binary that runs nothing and writes no statistics, (2) a corpus
with one deliberately broken golden (only that fixture fails; the others
still pass), (3) a fixture that never ends (a timeout is a harness failure,
never a pass), and (4) a "mutant" that is really the unmutated binary (no
witness may report a detection).

  controls.py --nift ./nift
"""
import argparse
import json
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUN = HERE / "run.py"


def gate(*args, env=None):
    p = subprocess.run([sys.executable, str(RUN), *args], capture_output=True, text=True,
                       env=dict(os.environ, **(env or {})))
    return p.returncode, p.stdout + p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    failures = []
    with tempfile.TemporaryDirectory(prefix="cp22-controls-") as d:
        # 1. Dead binary.
        dead = Path(d) / "dead"
        dead.write_text("#!/bin/sh\nexit 0\n")
        dead.chmod(0o755)
        rc, out = gate("--nift", str(dead), "--mode", "vm", "--only", "a01_basic")
        if rc == 0 or "statistics missing" not in out:
            failures.append(f"dead binary not rejected as a harness failure: rc={rc} {out[-300:]}")
        # 2. One broken golden: exactly that fixture fails.
        cases = Path(d) / "cases"
        shutil.copytree(HERE / "cases", cases)
        broken = cases / "a02_nested_many.stdout"
        broken.write_bytes(broken.read_bytes() + b"unrelated\n")
        rc, out = gate("--nift", nift, "--mode", "vm", "--cases", str(cases), "--only", "a0")
        fails = [line for line in out.splitlines() if line.startswith("FAIL vm ")]
        if rc == 0 or len(fails) != 1 or "a02_nested_many.f" not in fails[0]:
            failures.append(f"unrelated failure control: rc={rc} fails={fails}")
        # 3. A fixture that never ends.
        hang = Path(d) / "hang"
        hang.mkdir()
        (hang / "h_hang.f").write_text("fn[async](f() ) { return 1 }\ni := 0\nwhile(true) { sleep(1) }\n")
        for suffix, data in ((".stdout", b""), (".stderr", b""), (".exit", b"0\n")):
            (hang / ("h_hang" + suffix)).write_bytes(data)
        rc, out = gate("--nift", nift, "--mode", "vm", "--cases", str(hang), env={"CP22_TIMEOUT": "3"})
        if rc == 0 or "harness: timeout" not in out:
            failures.append(f"timeout control: rc={rc} {out[-300:]}")
        # 4. The unmutated binary posing as mutant 6 (snapshot write-back).
        info = Path(d) / "fake.json"
        sha = hashlib.sha256(Path(nift).read_bytes()).hexdigest()
        info.write_text(json.dumps({"mutant": 6, "sha256": sha, "normal_sha256": sha,
                                    "objects": {"vm_coro.o": [sha, sha]}}))
        rc, out = gate("--nift", nift, "--mutant", "6", "--oracle", nift, "--mutant-info", str(info))
        if rc == 0 or "provenance: the mutant is identical" not in out:
            failures.append(f"unmutated binary reported as a detection: rc={rc} {out[-300:]}")
    for f in failures:
        print("FAIL cp22 control: " + f)
    if failures:
        return 1
    print("PASS cp22 controls: dead binary, unrelated failure, timeout and unmutated-mutant controls all rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
