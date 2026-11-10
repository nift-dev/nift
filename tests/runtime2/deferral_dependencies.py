#!/usr/bin/env python3
"""Whole-program refusals that the dead-code name-certainty proposal depends on (C120).

docs/runtime-2/checkpoints/dead-code-admission.md section 5.2 (name certainty) proposes deferring a
callee-named site only when no unit of the program binds that name anywhere. The scan is sound only
while every construct that could bind a name invisibly to the VM stays a whole-program refusal, even
in code that never runs: template text and fragments, irregular (opaque) parses, nested imports and
dynamic imports. This test pins that each one still refuses the whole program before execution
(exit 1, nothing printed) while legacy completes it. If one of them stops refusing, the name scan
must be extended first: the test fails until it is.

The runtime is not changed by this test; it pins today's behaviour.

usage: deferral_dependencies.py --nift NIFT
"""
import argparse
import os
import subprocess
import sys
import tempfile

MODULE = "fn(answer()) { return 42 }\nexport(answer)\n"

CASES = [
    # (name, main.f, expected VM reason)
    ("template text in an uncalled lambda", 'f := () => { <b>$[nope()]</b> }\nprint("n")\n', "opaque:template-text"),
    ("irregular parse in an untaken branch", 'if (false) { p := os() == "x" ? 1 : 2 }\nprint("n")\n',
     "opaque:irregular-legacy-parse"),
    ("nested import in an untaken branch", 'if (false) { import("./m.f") }\nprint("n")\n', "import-nested"),
    ("nested import in an uncalled function", 'fn(u()) { import("./m.f") }\nprint("n")\n', "import-nested"),
    ("dynamic import", 'p := "./m.f"\nimport(p)\nprint("n")\n', "import-dynamic"),
]


def run(nift, d, legacy):
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env["HOME"] = d
    if legacy:
        env["NIFT_VM"] = "0"
    p = subprocess.run([nift, "main.f"], cwd=d, env=env, capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout, p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    nift = os.path.abspath(ap.parse_args().nift)
    failures = []
    for name, src, reason in CASES:
        with tempfile.TemporaryDirectory(prefix="deferral-deps-") as d:
            with open(os.path.join(d, "m.f"), "w") as f:
                f.write(MODULE)
            with open(os.path.join(d, "main.f"), "w") as f:
                f.write(src)
            rc, out, err = run(nift, d, False)
            if (rc, out) != (1, "") or ("not compiled for the VM (%s)" % reason) not in err:
                failures.append("%s: VM exit %d stdout %r stderr %r (expected a whole-program refusal, %s)" %
                                (name, rc, out, err[:200], reason))
            rc, out, err = run(nift, d, True)
            if (rc, out) != (0, "n\n"):
                failures.append("%s: legacy exit %d stdout %r stderr %r (recorded: completes, prints n)" %
                                (name, rc, out, err[:200]))
    for f in failures:
        print("FAIL deferral dependencies: " + f)
    print("%s deferral dependencies: %d whole-program refusal(s) the name-certainty scan relies on" %
          ("FAIL" if failures else "PASS", len(CASES)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
