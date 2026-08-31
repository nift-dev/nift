#!/usr/bin/env python3
"""Offline failure-mode checks for the repaired full-build scaling benchmark.

Proves the benchmark fails closed when a timed build does not rewrite all
expected outputs, when the expected output count is incomplete, and when a
timed build returns non-zero. Deterministic and offline; no timing evidence is
collected here.
"""
import argparse
import importlib.util
import pathlib
import subprocess
import sys
import tempfile

ap = argparse.ArgumentParser()
ap.add_argument("--nift", required=True)
args = ap.parse_args()

spec = importlib.util.spec_from_file_location(
    "full_build_scaling_benchmark",
    pathlib.Path(__file__).resolve().parent / "full_build_scaling_benchmark.py")
bm = importlib.util.module_from_spec(spec)
sys.argv = ["full_build_scaling_benchmark.py", "--nift", args.nift]
spec.loader.exec_module(bm)

fails = []


def check(label, fn):
    try:
        fn()
        fails.append(label)
        print(f"  [FAIL] {label}: benchmark did not fail closed")
    except SystemExit:
        print(f"  [PASS] {label}")


with tempfile.TemporaryDirectory(prefix="nift-failmode-") as td:
    root = pathlib.Path(td)
    bm.fixture(root, 4)
    fx = bm.Fixture(root, 4)
    fx.toggle()
    subprocess.run([args.nift, "build", "--all"], cwd=td,
                   stdout=subprocess.DEVNULL, check=True)

    def not_rewritten():
        # Corrupt one output so it lacks the current variant marker.
        (root / "public" / "p0.html").write_text("<main class='z'><p>0</p></main>\n")
        fx.verify_rewrite()
    check("timed build not rewriting all expected outputs fails closed", not_rewritten)

    def incomplete():
        (root / "public" / "p1.html").unlink()
        fx.verify_rewrite()
    check("incomplete expected output count fails closed", incomplete)

    # Restore a complete, correctly rewritten set, then force a failing build.
    fx.toggle()
    subprocess.run([args.nift, "build", "--all"], cwd=td,
                   stdout=subprocess.DEVNULL, check=True)
    saved = bm.args.nift
    bm.args.nift = "/bin/false"

    def nonzero():
        fx.timed_build()
    check("timed build returning non-zero fails closed", nonzero)
    bm.args.nift = saved

if fails:
    print(f"\nFAILED: {len(fails)}: {fails}")
    sys.exit(1)
print("\nALL FULL-BUILD-SCALING FAILURE-MODE CHECKS PASSED")