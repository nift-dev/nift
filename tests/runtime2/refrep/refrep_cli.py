#!/usr/bin/env python3
"""B2 reference representation (docs/runtime-2/checkpoints/b2-refrep.md), CLI lane.

  refrep_cli.py --nift ./nift

* Owner requirement A (session mutation atomicity), the receiver-loss
  regression: on an interactive session (`nift -i`, guard on) a failed array
  mutation (pop on empty, insert/remove/index out of range, invalid splice),
  repeated, never erases or nulls the binding; a captured reference still sees
  it, later successful mutations work, and the value persists across
  statements. Before B2 the take/put lowering left the binding null.
* Coordinator ruling 4: a global string `+=` keeps its in-place append (the
  compound-assignment take stays), so 400,000 appends finish well inside the
  limit (a per-append copy would move about 80 GB).
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

SESSION_IN = (
    "a := [1]\n"
    "f := () => a.length()\n"
    "a.pop()\n"
    "a.pop()\n"
    "a.pop()\n"
    "a.insert(5, 1)\n"
    "a.remove(3)\n"
    "a.splice(\"x\", 1)\n"
    "a[4] = 1\n"
    "print(f())\n"
    "a.push(7)\n"
    "print(a.stringify())\n"
    "print(f())\n"
    "b := [2]\n"
    "b.pop()\n"
    "b.pop()\n"
    "b.push(3)\n"
    "print(b.stringify())\n"
    "exit\n"
)
SESSION_OUT = "1\n0\n7\n[7]\n1\n2\n3\n[3]\n"
SESSION_ERR = [
    "error: pop: array is empty",
    "error: pop: array is empty",
    "error: insert: index out of range",
    "error: remove: index out of range",
    "error: splice: invalid index/count",
    "error: index out of range: 4",
    "error: pop: array is empty",
]

# Production configuration: the receiver as its own splice replacement stays
# refused by the guard (the probe configuration executes it: refrep_vm.cpp).
SPLICE_SELF = 'a := [1, 2, 3]\na.splice(1, 1, a)\nprint(a.stringify())\n'

LINEAR = 's := ""\ni := 0\nwhile (i < 400000) { s += "x"; i += 1 }\nprint(s.length())\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = []
    checks = 0

    def check(ok, what):
        nonlocal checks
        checks += 1
        if not ok:
            failures.append(what)
            print("FAIL " + what)

    env = dict(os.environ)
    env.pop("NIFT_VM", None)
    with tempfile.TemporaryDirectory(prefix="refrep-cli-") as work:
        p = subprocess.run([nift, "-i"], input=SESSION_IN, text=True, cwd=work, env=env, capture_output=True,
                           timeout=60)
        check(p.returncode == 0, "session: exit %d" % p.returncode)
        check(p.stdout == SESSION_OUT, "session: stdout %r (want %r)" % (p.stdout, SESSION_OUT))
        errors = [line for line in p.stderr.splitlines() if line.startswith("error:")]
        check(errors == SESSION_ERR, "session: errors %r (want %r)" % (errors, SESSION_ERR))
        check("null" not in p.stdout, "session: a binding became null")

        path = os.path.join(work, "splice_self.f")
        with open(path, "w") as f:
            f.write(SPLICE_SELF)
        p = subprocess.run([nift, path], text=True, cwd=work, env=env, capture_output=True, timeout=60)
        # B5 (b5-refsem.md section 6): admitted; every splice argument is evaluated before the one
        # commit (D-NEXT-refsem #4 ruling, B2 self-splice), so the replacement is the entry value:
        # [1,1,2,3,3]. Legacy removes first ([1,1,3,3]): the approved splice-order difference.
        check(p.returncode == 0 and p.stdout == "[1,1,2,3,3]\n" and p.stderr == "",
              "splice-self: %d %r %r" % (p.returncode, p.stdout, p.stderr[:200]))

        path = os.path.join(work, "linear.f")
        with open(path, "w") as f:
            f.write(LINEAR)
        start = time.monotonic()
        p = subprocess.run([nift, path], text=True, cwd=work, env=env, capture_output=True, timeout=60)
        elapsed = time.monotonic() - start
        check(p.returncode == 0 and p.stdout == "400000\n", "linear: %d %r %r" % (p.returncode, p.stdout, p.stderr))
        check(elapsed < 20, "linear: global string += took %.1f s (quadratic?)" % elapsed)
    print("%s refrep-cli: %d checks, %d failure(s)" % ("PASS" if not failures else "FAIL", checks, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
