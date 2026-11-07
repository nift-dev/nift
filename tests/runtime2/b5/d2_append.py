#!/usr/bin/env python3
"""D2 linear-append guard (docs/runtime-2/checkpoints/combined-b3-b5.md section 27).

  d2_append.py --nift NIFT

A compound `s += f(i)` whose rhs is a call takes compound_snapshot (the target is read before the
rhs). When the rhs leaves the binding unchanged the append must still run in place on the binding
(the unchanged-binding path), so a 1,200,000-step loop stays linear: at top level (the binding's own
register), in a `fn` and in a lambda on a top-level binding (a global). Each must print the length
and finish within 20 s (about 0.2 s when linear; a copy per append takes over 60 s). The same bound
as refrep_cli.py's literal `s += "x"` guard. Sessions and captured cells are not in place on
either side of D2 (pre-existing, recorded in section 27) and are not checked here.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

N = 1200000
PROGRAMS = {
    "top": 's := ""\nfn(piece(n)) { return "x" }\ni := 0\nwhile (i < %d) { s += piece(i); i += 1 }\n'
           'print(s.length())\n' % N,
    "fnglob": 's := ""\nfn(piece(n)) { return "x" }\n'
              'fn(fill()) { i := 0; while (i < %d) { s += piece(i); i += 1 }; return 0 }\nfill()\n'
              'print(s.length())\n' % N,
    "lam": 's := ""\npiece := (n) => { return "x" }\n'
           'fill := () => { i := 0; while (i < %d) { s += piece(i); i += 1 }; return 0 }\nfill()\n'
           'print(s.length())\n' % N,
}

GROW = 's := "a"\nw := "0123456789"\ni := 0\nwhile (i < 17) { w += w; i += 1 }\n'
BUDGET_SESSIONS = {
    "plain": GROW + 's += w\ns += w\nprint("after")\nprint(s.length())\n',
    "rebinding": GROW + 'k := () => { s = "Z"; return w }\ns += w\ns += k()\nprint("after")\nprint(s.length())\n',
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    env = dict(os.environ, NIFT_VM="require")
    failures = 0
    with tempfile.TemporaryDirectory(prefix="d2-append-") as work:
        for name, src in sorted(PROGRAMS.items()):
            path = os.path.join(work, name + ".f")
            with open(path, "w") as f:
                f.write(src)
            start = time.monotonic()
            try:
                p = subprocess.run([nift, path], text=True, cwd=work, env=env, capture_output=True, timeout=60)
                out, err, code = p.stdout, p.stderr, p.returncode
            except subprocess.TimeoutExpired:
                out, err, code = "", "timeout", 124
            elapsed = time.monotonic() - start
            ok = code == 0 and out == "%d\n" % N and elapsed < 20
            failures += not ok
            print("%s d2-append %s: exit %d, %r, %.2f s%s" % ("PASS" if ok else "FAIL", name, code, out.strip(),
                                                             elapsed, "" if ok else " " + err.strip()[:200]))
        # Release audit L1 (review of 3b513751): a memory-budget stop during a session's string append
        # (a plain `s += w` and a rebinding `s += k()`) ends the session: exit 70, no later statement
        # runs, so the binding's state after the fault is never read. Pinned for these triggers only,
        # not proven for every path (combined-b3-b5.md section 27.5).
        for name, src in sorted(BUDGET_SESSIONS.items()):
            env_b = dict(env, NIFT_VM_HEAP_BUDGET="3000000")
            try:
                p = subprocess.run([nift, "-i"], input=src, text=True, cwd=work, env=env_b, capture_output=True,
                                   timeout=60)
                out, err, code = p.stdout, p.stderr, p.returncode
            except subprocess.TimeoutExpired:
                out, err, code = "", "timeout", 124
            ok = code == 70 and "memory budget exceeded" in err and "after" not in out
            failures += not ok
            print("%s d2-append budget %s: exit %d, later statements ran: %s" % (
                "PASS" if ok else "FAIL", name, code, "after" in out))
    total = len(PROGRAMS) + len(BUDGET_SESSIONS)
    print("%s d2-append: %d programs, %d failure(s)" % ("PASS" if not failures else "FAIL", total, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
