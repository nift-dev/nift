#!/usr/bin/env python3
"""Historical callback witnesses under the existing CP14B comparator profiles."""
import argparse
import os
from pathlib import Path
import tempfile

from comparator_profile import banner, profile, refused_ok
from functional import run

CASES = (
    ("a := [3,1,2]\na.sort((x,y) => x < y)\nprint(a.join(\",\"))", 0, "1,2,3\n", ""),
    ("a := [2,1]; a.sort((x,y) => x + y)", 1, "", "comparator must return bool"),
)


def check_channels(source, result, expected_exit, expected_out, expected_error):
    error_ok = expected_error in result["err"] if expected_error else not result["err"]
    if result["exit"] != expected_exit or result["out"] != expected_out or not error_ok:
        raise SystemExit(f"FAIL historical comparator {source!r}: {result!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True, type=Path)
    args = parser.parse_args()
    binary = str(args.nift.resolve())
    legacy_only = os.environ.get("NIFT_VM") == "0"
    active = None if legacy_only else profile(binary)
    with tempfile.TemporaryDirectory(prefix="v48-comparator-") as folder:
        root = Path(folder)
        for source, *expected in CASES:
            legacy = run(binary, "0", source, root)
            check_channels(source, legacy, *expected)
            if legacy_only:
                continue
            if active.refused:
                ok, detail = refused_ok(active, binary, binary, source, root)
                if not ok:
                    raise SystemExit(f"FAIL historical comparator profile {source!r}: {detail!r}")
                continue
            vm = run(binary, "require", source, root)
            check_channels(source, vm, *expected)
            stats = vm["stats"][-1] if vm["stats"] else {}
            if stats.get("vm_executions") != 1 or stats.get("legacy_executions") != 0:
                raise SystemExit(f"FAIL historical comparator execution counters: {stats!r}")
    print("PASS comparator legacy witnesses" if legacy_only else banner(active, len(CASES), "v4.8 callbacks"))


if __name__ == "__main__":
    main()
