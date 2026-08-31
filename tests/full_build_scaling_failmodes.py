#!/usr/bin/env python3
"""Offline failure-mode and invocation checks for the repaired full-build
scaling benchmark.

Proves the benchmark fails closed when a timed build does not rewrite all
expected outputs, when the expected output count is incomplete, and when a
timed build returns non-zero - exercising the complete Fixture.timed_build()
path (with a mocked successful no-op subprocess for the rewrite/count cases)
rather than calling the checker directly. Also proves every documented
invocation invariant is enforced with a controlled early diagnostic.
Deterministic and offline; no timing evidence is collected here.
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

BENCH = pathlib.Path(__file__).resolve().parent / "full_build_scaling_benchmark.py"

spec = importlib.util.spec_from_file_location(
    "full_build_scaling_benchmark", BENCH)
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


def check_cond(label, cond):
    if not cond:
        fails.append(label)
        print(f"  [FAIL] {label}")
    else:
        print(f"  [PASS] {label}")


class FakeSuccess:
    returncode = 0
    stderr = b""


def mock_build_success():
    bm.subprocess.run = lambda *a, **k: FakeSuccess()


with tempfile.TemporaryDirectory(prefix="nift-failmode-") as td:
    root = pathlib.Path(td)
    bm.fixture(root, 4)
    fx = bm.Fixture(root, 4)
    fx.toggle()
    subprocess.run([args.nift, "build", "--all"], cwd=td,
                   stdout=subprocess.DEVNULL, check=True)

    saved_run = bm.subprocess.run

    # 1. A timed build that does not rewrite every output fails closed. The
    #    build itself is mocked to succeed; the real verify_rewrite inside
    #    timed_build must still reject the stale outputs.
    mock_build_success()
    for f in (root / "public").glob("*.html"):
        f.write_text("<main class='z'><p>x</p></main>\n")
    check("timed build not rewriting all expected outputs fails closed",
          lambda: fx.timed_build())
    bm.subprocess.run = saved_run

    # 2. Incomplete expected output count fails closed (via timed_build).
    mock_build_success()
    (root / "public" / "p1.html").unlink()
    check("incomplete expected output count fails closed",
          lambda: fx.timed_build())
    bm.subprocess.run = saved_run

    # 3. A timed build returning non-zero fails closed (real failing build).
    saved = bm.args.nift
    bm.args.nift = "/bin/false"
    check("timed build returning non-zero fails closed", lambda: fx.timed_build())
    bm.args.nift = saved

    # 4. Invocation invariants are enforced with a controlled early diagnostic.
    for argv, needle in [
        (["--small", "0"], "small"),
        (["--small", "2000", "--large", "6000"], "large"),
        (["--rounds", "3"], "rounds"),
        (["--confirm-rounds", "3"], "confirm-rounds"),
        (["--max-ratio", "0"], "max-ratio"),
    ]:
        p = subprocess.run([sys.executable, str(BENCH), "--nift", args.nift, *argv],
                           capture_output=True, text=True)
        label = "invalid invocation %s fails with a controlled diagnostic" % " ".join(argv)
        check_cond(label, p.returncode != 0 and needle in (p.stdout + p.stderr))

if fails:
    print(f"\nFAILED: {len(fails)}: {fails}")
    sys.exit(1)
print("\nALL FULL-BUILD-SCALING FAILURE-MODE CHECKS PASSED")