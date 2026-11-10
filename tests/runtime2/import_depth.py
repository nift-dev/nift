#!/usr/bin/env python3
"""Import chain depth (owner 2026-11-11 02:53, decisions.md D-067 #4).

An import chain 33 modules deep runs on the VM (approved difference: legacy fails at depth 33 with
"maximum template parse depth exceeded"); a chain 64 deep is refused before execution with
`import-limit` (kMaxModuleDepth = 64, src/rt/module_graph.h: the resource bound of the recursive
module-graph walk). The runtime is not changed by this test; it pins the three behaviours.

usage: import_depth.py --nift NIFT
"""
import argparse
import os
import subprocess
import sys
import tempfile


def chain(d, depth):
    with open(os.path.join(d, "main.f"), "w") as f:
        f.write('import("./m1.f")\nprint(x)\n')
    for i in range(1, depth):
        with open(os.path.join(d, "m%d.f" % i), "w") as f:
            f.write('import("./m%d.f")\nexport(x)\n' % (i + 1))
    with open(os.path.join(d, "m%d.f" % depth), "w") as f:
        f.write("x := 1\nexport(x)\n")


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
    with tempfile.TemporaryDirectory(prefix="import-depth-") as d:
        chain(d, 33)
        rc, out, err = run(nift, d, False)
        if (rc, out) != (0, "1\n"):
            failures.append("depth 33 on the VM: exit %d stdout %r stderr %r" % (rc, out, err[:200]))
        rc, out, err = run(nift, d, True)
        if rc != 1 or "maximum template parse depth exceeded" not in err:
            failures.append("depth 33 on legacy (recorded failure): exit %d stderr %r" % (rc, err[-200:]))
    with tempfile.TemporaryDirectory(prefix="import-depth-") as d:
        chain(d, 64)
        rc, out, err = run(nift, d, False)
        if (rc, out) != (1, "") or "not compiled for the VM (import-limit)" not in err:
            failures.append("depth 64 on the VM: exit %d stdout %r stderr %r" % (rc, out, err[:200]))
    for f in failures:
        print("FAIL import depth: " + f)
    print("%s import depth: VM runs depth 33 (legacy fails there), refuses depth 64 (import-limit)" %
          ("FAIL" if failures else "PASS"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
