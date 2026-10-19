#!/usr/bin/env python3
"""CP12 phase B: CP08-style probes of legacy `import`/`export` semantics.

  semantics_modules.py --nift ./nift [--record]

Each probe is a directory tests/runtime2/semantics-modules/<probe>/ with the
program in p/ (entry p/main.f, modules beside it) and the expected result of
running `nift main.f` in p/: expected.stdout, expected.stderr (the probe
directory's absolute path replaced by `$P`), expected.exit. It is also an
rt-diff workspace (rt-diff.workspace names `p`). --record writes the
expected files from the binary (legacy is the oracle, D-015); without it the
binary must reproduce them exactly. NIFT_VM in the environment selects the
engine (unset: legacy).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "semantics-modules")


def run(nift, probe, work):
    dst = os.path.join(work, probe)
    shutil.copytree(os.path.join(HERE, probe, "p"), os.path.join(dst, "p"))
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": work, "LANG": "C"}
    for key in ("NIFT_VM", "NIFT_VM_STATS"):
        if key in os.environ:
            env[key] = os.environ[key]
    p = subprocess.run(["timeout", "30", nift, "main.f"], cwd=os.path.join(dst, "p"), env=env, capture_output=True)
    norm = lambda b: b.replace(os.path.realpath(dst).encode(), b"$P").replace(dst.encode(), b"$P")  # noqa: E731
    return norm(p.stdout), norm(p.stderr), str(p.returncode).encode() + b"\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--record", action="store_true")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = 0
    probes = sorted(d for d in os.listdir(HERE) if os.path.isdir(os.path.join(HERE, d, "p")))
    with tempfile.TemporaryDirectory(prefix="sem-mod-") as work:
        for probe in probes:
            got = run(nift, probe, work)
            names = ("expected.stdout", "expected.stderr", "expected.exit")
            if args.record:
                for name, data in zip(names, got):
                    with open(os.path.join(HERE, probe, name), "wb") as f:
                        f.write(data)
                continue
            want = tuple(open(os.path.join(HERE, probe, n), "rb").read() for n in names)
            if got != want:
                failures += 1
                print(f"FAIL {probe}: got {got!r}, expected {want!r}")
    print(f"{'PASS' if not failures else 'FAIL'} semantics modules: {len(probes)} probes, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
