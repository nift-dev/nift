#!/usr/bin/env python3
"""CP12 phase B: NIFT_VM=classify-all (src/rt/refusal_inventory.h).

  vm_inventory.py --nift ./nift

Checks, per case: classify-all reports exactly classify's outcome and first
reason; the set of non-consequential reasons is the expected one; nothing
executes (no output, exit 0, no file written); and the free-name refinement
of the globals analysis (src/rt/globals.h) gives the expected verdict.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

CASES = [
    # (name, program, expected certain reasons)
    ("compiles", "x := 1\nprint(x)\n", set()),
    ("two-methods", 's := "abc"\nprint(s.foo())\nprint(s.bar())\n', {"method:foo", "method:bar"}),
    # CP12b's chained postfix admits the chain and CP14 has `trim` and
    # `to_upper` natives, so the program compiles (no reasons); vm_chain.py
    # runs the same chain on the VM against legacy in every context.
    ("chained", 's := " a "\nprint(s.trim().to_upper())\n', set()),
    # CP12b: compiled outside loop code (textual wording, L43); refused in loops.
    ("chained-member", 'fn(f()) { return {"k": 1} }\nprint(f().k)\n', set()),
    ("chained-member-loop", 'fn(f()) { return {"k": 1} }\ni := 0\nwhile (i < 1) {\n  i += 1\n  print(f().k)\n}\n',
     {"postfix-receiver:member"}),
    ("lambda-argument", "xs := [1, 2]\ny := xs.map((v) => v + 1)\nprint(y.join(\",\"))\n",
     {"method:map", "method:join"}),
    ("for-and-try", "for (i : [1, 2]) {\n  print(i.zap())\n}\ntry {\n  print(1)\n  nothere()\n} catch (e) {\n"
     "  print(e)\n}\n", {"statement:For", "method:zap", "statement:Try", "callable:nothere"}),
    ("fn-after-global", "g := 10\nfn(f(x)) {\n  return x + g\n}\nprint(f(1))\n",
     set()),  # CP12b: a declaration after statements and a resolved global compile
    ("global-before-declaration", "fn(f(x)) {\n  g = g + 1\n  return x + g\n}\nprint(f(1))\ng := 10\n",
     {"assign-free-name:before-declaration", "free-name:before-declaration"}),
    ("dynamic-caller", "fn(inner()) { return z }\nfn(outer()) { z := 1\nreturn inner() }\nprint(outer())\n",
     {"free-name:dynamic-caller"}),
    ("dynamic-shadow", "z := 5\nfn(inner()) { return z }\nfn(outer()) { z := 1\nreturn inner() }\n"
     "print(outer())\n", {"free-name:dynamic-shadow"}),
    ("called-before-declaration", "print(f())\nfn(f()) { return 1 }\n", {"fn-called-before-declaration"}),
    # CP12b: structs compile; an instance is made only by `p := P()`.
    ("struct", "struct (P) { x := 1 }\nprint(P().x)\n", {"struct-construction"}),
    ("parse-error", "fn(f()) {\nprint(1)\n", {"parse-error"}),
]


# Programs CP13's prevalidation (D-022 #3, src/CLI.cpp) rejects before an
# engine is selected: no statistics are written with it on. Their inventory
# reason is checked with NIFT_PREVALIDATE=off; with it on, the run must fail
# with the diagnostic and write nothing.
PREVALIDATION_REJECTS = {"parse-error"}


def run(nift, program, mode, work, extra=None):
    path = os.path.join(work, "t.f")
    with open(path, "w") as f:
        f.write(program)
    stats = os.path.join(work, "stats.jsonl")
    if os.path.exists(stats):
        os.remove(stats)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": work, "NIFT_VM": mode, "NIFT_VM_STATS": stats}
    env.update(extra or {})
    p = subprocess.run(["timeout", "60", nift, "t.f"], cwd=work, env=env, capture_output=True)
    try:
        return p, json.loads(open(stats).read().splitlines()[-1])
    except (OSError, IndexError, ValueError):
        # No statistics line: reported as this case's failure, not a harness crash.
        return p, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = 0
    with tempfile.TemporaryDirectory(prefix="vm-inventory-") as work:
        for name, program, want in CASES:
            extra = None
            if name in PREVALIDATION_REJECTS:
                p0, none = run(nift, program, "classify-all", work)
                if none is not None or p0.returncode == 0 or b"error:" not in p0.stderr:
                    failures += 1
                    print(f"FAIL {name}: prevalidation should reject before the engine (exit {p0.returncode}, "
                          f"statistics {none is not None}, stderr {p0.stderr[-120:]!r})")
                    continue
                extra = {"NIFT_PREVALIDATE": "off"}
            p1, classify = run(nift, program, "classify", work, extra)
            p2, every = run(nift, program, "classify-all", work, extra)
            missing = [(mode, p) for mode, p, st in (("classify", p1, classify), ("classify-all", p2, every))
                       if st is None]
            if missing:
                failures += 1
                print(f"FAIL {name}: no statistics line: " + "; ".join(
                    f"NIFT_VM={mode} exit {p.returncode}, stderr {p.stderr[-200:]!r}" for mode, p in missing))
                continue
            got = {r["reason"] for r in every["reasons"] if not r["consequential"]}
            problems = []
            if p2.returncode != 0 or p2.stdout:
                problems.append(f"executed or failed (exit {p2.returncode}, stdout {p2.stdout[:60]!r})")
            if (every["outcome"], every["reason"]) != (classify["outcome"], classify["reason"]):
                problems.append(f"outcome {every['outcome']}/{every['reason']} != classify "
                                f"{classify['outcome']}/{classify['reason']}")
            if every["reasons"] and every["reasons"][0]["reason"].split(":")[0] != classify["reason"].split(":")[0]:
                problems.append(f"first reason {every['reasons'][0]['reason']} != {classify['reason']}")
            if got != want:
                problems.append(f"certain reasons {sorted(got)} != {sorted(want)}")
            if sorted(os.listdir(work)) != ["stats.jsonl", "t.f"]:
                problems.append(f"files written: {sorted(os.listdir(work))}")
            if problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name}: {sorted(got)}")
    print(f"vm_inventory: {len(CASES)} cases, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
