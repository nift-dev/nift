#!/usr/bin/env python3
"""The for-loop/catch binding rule (src/rt/binding_rules.h, record §12.1.4).

  binding.py --nift ./nift            every binding/*.f, with prevalidation on
                                      (the contractual default), on legacy
                                      (NIFT_VM unset) and on the VM default
                                      (NIFT_VM=1): both == the golden, and the
                                      run leaves no file behind
  binding.py --nift ./nift --record   (re)write the goldens from legacy

bad_*.f: a for-loop or catch binding with a built-in's name is called in its
scope. Both engines reject the unit before anything of it runs: no output
on stdout, one diagnostic, exit 1, no effect (an imported module: when the
import runs, before any of the module's statements). ok_*.f: valid uses (the binding
read as a value, built-in calls outside the scope, members, text) run, with
the same output on both engines.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent / "binding"
TIMEOUT = int(os.environ.get("CP22_TIMEOUT", "120"))


def run(nift, name, vm):
    with tempfile.TemporaryDirectory(prefix="cp22b-") as d:
        shutil.copy(HERE / name, Path(d) / name)
        module = HERE / (name[:-2] + ".m.nft")
        if module.exists():
            shutil.copy(module, Path(d) / "m.nft")
        before = set(os.listdir(d))
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C",
               "LC_ALL": "C", "TZ": "UTC"}
        if vm:
            env["NIFT_VM"] = vm
        try:
            p = subprocess.run([nift, name], cwd=d, env=env, stdin=subprocess.DEVNULL, capture_output=True,
                               timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            return (b"", b"", "timeout"), []
        created = sorted(set(os.listdir(d)) - before)
        out, err = p.stdout.replace(d.encode(), b"$ROOT"), p.stderr.replace(d.encode(), b"$ROOT")
        return (out, err, p.returncode), created


def golden(name):
    stem = HERE / name[:-2]
    try:
        return (Path(str(stem) + ".stdout").read_bytes(), Path(str(stem) + ".stderr").read_bytes(),
                int(Path(str(stem) + ".exit").read_text().strip()))
    except OSError:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--record", action="store_true")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures, count = 0, 0
    for path in sorted(HERE.glob("*.f")):
        name = path.name
        if args.record:
            result, _ = run(nift, name, None)
            stem = HERE / name[:-2]
            Path(str(stem) + ".stdout").write_bytes(result[0])
            Path(str(stem) + ".stderr").write_bytes(result[1])
            Path(str(stem) + ".exit").write_text(f"{result[2]}\n")
            print(f"recorded {name}: exit {result[2]}")
            continue
        count += 1
        want = golden(name)
        problems = []
        if want is None:
            problems.append("missing golden")
        for label, vm in (("legacy", None), ("vm", "1")):
            result, created = run(nift, name, vm)
            if result[2] == "timeout" or not isinstance(result[2], int) or not 0 <= result[2] < 128:
                problems.append(f"{label}: harness: abnormal end {result[2]}")
                continue
            if want is not None and result != want:
                problems.append(f"{label}: {result!r} != golden {want!r}")
            if created:
                problems.append(f"{label}: left files {created}")
        if want is not None and name.startswith("bad_"):
            # An imported module is validated when the import runs: what ran
            # before the import stays; nothing of the module runs.
            before = b"before\n" if name.startswith("bad_module") else b""
            if want[0] != before or want[2] != 1 or b"has the name of a built-in and is called in its scope" not in want[1]:
                problems.append(f"golden is not the binding diagnostic: {want!r}")
        if want is not None and name.startswith("ok_") and want[2] != 0:
            problems.append(f"valid program golden fails: {want!r}")
        if problems:
            failures += 1
            print(f"FAIL binding {name}: " + "; ".join(problems))
        else:
            print(f"ok   binding {name}")
    if args.record:
        return 0
    print(f"{'PASS' if failures == 0 else 'FAIL'} cp22 binding: {count} case(s), {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
