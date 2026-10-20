#!/usr/bin/env python3
"""Regression tests for Decimal literals through observable typed parameters.

The zero-valued decimal and huge-exponent spellings decode to Decimal. In loop
and loop-called-function code, legacy's prepared path and the canonical path
disagree about the parameter's type. The VM must either refuse with the named
D-018 reason (context:parameter-type) or execute and match legacy exactly. It
must never silently execute a divergent assignment.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

SPELLINGS = [
    ("0.0", False),
    ("1e300", False),
    ("1.7976931348623157e308", False),
    ("5e-324", True),
]


def run(nift, text, vm):
    with tempfile.TemporaryDirectory(prefix="nift-decimal-") as folder:
        path = os.path.join(folder, "case.f")
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
        env = dict(os.environ)
        env["NIFT_VM"] = "require" if vm else "0"
        env.pop("NIFT_VM_STATS", None)
        result = subprocess.run([nift, path], cwd=folder, capture_output=True, text=True,
                                timeout=60, env=env)
        return result.stdout, result.stderr, result.returncode


def final_error(text):
    return re.sub(r"^error: .*?:\d+:\d+: ", "", text.strip())


def contexts(spelling):
    loop = (
        "fn(f(x)) { x = 1.5; print(x) }\n"
        "i := 0\n"
        f"while(i < 1) {{ f({spelling}); i += 1 }}\n"
    )
    fn = (
        "fn(f(x)) { x = 1.5; print(x) }\n"
        "fn(case(v)) { f(v) }\n"
        "i := 0\n"
        f"while(i < 1) {{ case({spelling}); i += 1 }}\n"
    )
    return {"loop": loop, "fn-from-loop": fn}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True)
    parser.add_argument("--oracle", default=None)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    nift = os.path.abspath(args.nift)
    oracle = os.path.abspath(args.oracle) if args.oracle else nift
    passed = failed = refused = 0

    for spelling, legacy_runs in SPELLINGS:
        for context, text in contexts(spelling).items():
            name = "decimal_param_%s_%s" % (
                spelling.replace("-", "neg").replace(".", "_"), context.replace("-", "_"))
            legacy = run(oracle, text, vm=False)
            vm = run(nift, text, vm=True)

            if legacy_runs:
                if legacy[2] != 0 or legacy[0] != "1.5\n":
                    failed += 1
                    print(f"HARNESS {name}: unexpected legacy result rc={legacy[2]} out={legacy[0]!r}")
                    print(f"  legacy err={legacy[1]!r}")
                    continue
            elif legacy[2] == 0 or "cannot change binding type" not in legacy[1]:
                failed += 1
                print(f"HARNESS {name}: unexpected legacy result rc={legacy[2]} out={legacy[0]!r}")
                print(f"  legacy err={legacy[1]!r}")
                continue

            if vm[2] == 0:
                if legacy_runs and vm[0] == legacy[0] and vm[1] == legacy[1]:
                    passed += 1
                    if args.verbose:
                        print(f"PASS {name}: VM execution matches legacy")
                else:
                    failed += 1
                    print(f"FAIL {name}: SILENT DIVERGENCE (VM ran where legacy refused)")
                    print(f"  legacy rc={legacy[2]} out={legacy[0]!r} err={legacy[1]!r}")
                    print(f"  vm     rc={vm[2]} out={vm[0]!r} err={vm[1]!r}")
            elif vm[2] != 70 and "not compiled for the VM" in vm[1] and \
                    "context:parameter-type" in vm[1] and not legacy_runs:
                refused += 1
                if args.verbose:
                    print(f"REFUSED {name}: context:parameter-type")
            else:
                failed += 1
                print(f"FAIL {name}: unexpected VM result rc={vm[2]}")
                print(f"  legacy rc={legacy[2]} out={legacy[0]!r} err={final_error(legacy[1])!r}")
                print(f"  vm     rc={vm[2]} out={vm[0]!r} err={final_error(vm[1])!r}")

    print(f"PASS {passed}, FAIL {failed}, REFUSED {refused}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
