#!/usr/bin/env python3
"""Loop-literal regression (v4.12.0 regressions 55cb323c and 5fe38bd1).

Each case runs at top level, inside a while loop, and in a function called from
a loop. All three must print the same bytes, and those bytes must equal the
pinned expected output recorded from Nift v4.6.0 built from its tag
(expected/<case>.out). v4.6.0 is the authoritative baseline: it interpolated
`$[...]` and decoded escapes in every context. The suite is not a statement
about earlier releases (v4.5.0 differs in 3 checks). Known results:
v4.11.0 2 failures (interpolated declarations in loops, an older defect),
v4.12.0 26, runtime-2 before 55cb323c/5fe38bd1 fixes 26, after 0.
Usage: run.py --nift PATH
"""
import argparse, os, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
def programs(stmt):
    body = stmt.replace("; ", "\n")
    top = "x := 7\n" + body + "\n"
    loop = "i := 0\nwhile(i < 1) {\n  x := 7\n" + "".join("  " + l + "\n" for l in body.split("\n")) + "  i += 1\n}\n"
    fn = "fn(case()) {\n  x := 7\n" + "".join("  " + l + "\n" for l in body.split("\n")) + "}\ni := 0\nwhile(i < 1) {\n  case()\n  i += 1\n}\n"
    return {"top": top, "loop": loop, "fn-from-loop": fn}
def run(nift, text):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "case.f")
        open(p, "w").write(text)
        r = subprocess.run([nift, p], cwd=d, capture_output=True, timeout=60, env=dict(os.environ, NIFT_VM="0"))
        return r.stdout + r.stderr, r.returncode
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--nift", required=True); ap.add_argument("--record", action="store_true")
    a = ap.parse_args(); a.nift = os.path.abspath(a.nift); fails = 0; n = 0
    for line in open(os.path.join(HERE, "cases.txt")):
        if not line.strip(): continue
        name, stmt = line.rstrip("\n").split("\t", 1)
        exp_path = os.path.join(HERE, "expected", name + ".out")
        outs = {ctx: run(a.nift, text) for ctx, text in programs(stmt).items()}
        if a.record:
            os.makedirs(os.path.dirname(exp_path), exist_ok=True); open(exp_path, "wb").write(outs["top"][0]); continue
        expected = open(exp_path, "rb").read()
        for ctx, (out, rc) in outs.items():
            n += 1
            if out != expected or rc != 0:
                fails += 1; print(f"FAIL {name} [{ctx}] exit {rc}: got {out!r}, want {expected!r}")
    if a.record: print("recorded"); return 0
    print(f"{'PASS' if not fails else 'FAIL'} loop literals: {n} checks, {fails} failure(s)")
    return 1 if fails else 0
if __name__ == "__main__": sys.exit(main())
