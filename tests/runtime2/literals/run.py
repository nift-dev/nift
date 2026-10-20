#!/usr/bin/env python3
"""CP15B literal/interpolation differential tests.

Each success case is generated at top level, inside a while loop, and inside a
function called from a loop. The VM (NIFT_VM=require) must execute it and match
the corrected legacy oracle (NIFT_VM=0) byte-for-byte. Refusal cases must be
rejected by the VM with a named reason; they must never be counted as VM
execution.

Usage: run.py --nift PATH [--oracle PATH]
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def run(nift, text, vm):
    with tempfile.TemporaryDirectory(prefix="nift-literals-") as folder:
        path = os.path.join(folder, "case.f")
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
        env = dict(os.environ)
        env["NIFT_VM"] = "require" if vm else "0"
        env.pop("NIFT_VM_STATS", None)
        result = subprocess.run([nift, path], cwd=folder, capture_output=True, text=True,
                                timeout=60, env=env)
        return result.stdout, result.stderr, result.returncode


def contexts(prelude, body):
    top = prelude + "x := 7\n" + body + "\n"
    indented = "".join("  " + line + "\n" for line in body.splitlines())
    loop = prelude + "i := 0\nwhile(i < 1) {\n  x := 7\n" + indented + "  i += 1\n}\n"
    fn = prelude + "fn(case()) {\n  x := 7\n" + indented + "}\ni := 0\nwhile(i < 1) {\n  case()\n  i += 1\n}\n"
    return {"top": top, "loop": loop, "fn-from-loop": fn}


# (name, prelude, body)
SUCCESS = [
    ("interp_double", "", 'print("v=$[x]!")'),
    ("interp_single", "", "print('v=$[x]!')"),
    ("interp_nested", "", 'print("v=$[[1,2][0]]!")'),
    ("interp_decl", "", 'y := "d=$[x]!"; print(y)'),
    ("interp_fn", 'fn(num()) { return 7 }\nfn(text()) { return "x" }\n',
     'print("a=$[num()] b=$[text()]")'),
    ("interp_side", 'fn(f1()) { print("f1"); return 1 }\nfn(f2()) { print("f2"); return 2 }\n',
     'print("x=$[f1()]$[f2()]")'),
    ("interp_values", "",
     'y := 1.5\nz := true\nn := null\ns := "café"\nprint("x=$[x] y=$[y] z=$[z] n=$[n] s=$[s]")'),
    ("interp_int64", "", 'print("a=$[9223372036854775807] b=$[-9223372036854775808]")'),
    ("escape_n", "", 'print("a\\nb")'),
    ("escape_t", "", 'print("a\\tb")'),
    ("escape_bs", "", 'print("a\\\\b")'),
    ("escape_unknown", "", 'print("q=\\q")'),
    ("escape_quote", "", 'print("q=\\\"r")'),
    ("escape_concat", "", 'print("c=" + "$[x]" + "\\n-")'),
    ("dollar_plain", "", 'print("a$b")'),
    ("dollar_shell", "", 'print("$HOME;$(touch nope)")'),
    ("int64_plain", "", 'print(9223372036854775807)\nprint(-9223372036854775808)\nprint(9007199254740993)'),
]

# (name, prelude, body, expected VM refusal reason)
REFUSED = [
    ("interp_array", "", 'print("v=$[[1,2]]")', "interpolation:non-scalar"),
    ("interp_unknown", 'o := {"x": 1}\nfn(get(o)) { return o.x }\n', 'print("a=$[get(o)]")',
     "interpolation:unknown-type"),
    ("hex_literal", "", "print(0x10)", "opaque:legacy-runtime-error"),
    ("overflow_exponent", "", "print(1e309)", "number-literal"),
    ("zero_decimal", "", "print(0.0)", "number-literal"),
    ("leading_zero", "", "print(01)", "number-literal"),
    ("escaped_dollar", "", 'print("a\\$[1]")', "opaque:irregular-legacy-parse"),
]

ERROR_SIDE_EFFECT = ("interp_error",
                     'fn(f1()) { print("f1"); return 1 }\n'
                     'fn(f2()) { print("f2"); return 1 / 0 }\n'
                     'fn(f3()) { print("f3"); return 3 }\n',
                     'print("$[f1()]$[f2()]$[f3()]")')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True)
    parser.add_argument("--oracle", default=None)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    nift = os.path.abspath(args.nift)
    oracle = os.path.abspath(args.oracle) if args.oracle else nift
    passed = failed = refused = 0

    for name, prelude, body in SUCCESS:
        for context, text in contexts(prelude, body).items():
            want = run(oracle, text, vm=False)
            got = run(nift, text, vm=True)
            if got != want:
                failed += 1
                print(f"FAIL {name} [{context}]")
                print(f"  oracle rc={want[2]} out={want[0]!r} err={want[1]!r}")
                print(f"  vm     rc={got[2]} out={got[0]!r} err={got[1]!r}")
            else:
                passed += 1
                if args.verbose:
                    print(f"PASS {name} [{context}] rc={got[2]} out={got[0]!r}")

    name, prelude, body = ERROR_SIDE_EFFECT
    for context, text in contexts(prelude, body).items():
        want = run(oracle, text, vm=False)
        got = run(nift, text, vm=True)
        if got[0] != want[0] or got[2] != want[2] or "f3" in got[0] or "division by zero" not in got[1]:
            failed += 1
            print(f"FAIL {name} [{context}] side effects/error")
            print(f"  oracle rc={want[2]} out={want[0]!r} err={want[1]!r}")
            print(f"  vm     rc={got[2]} out={got[0]!r} err={got[1]!r}")
        else:
            passed += 1
            if args.verbose:
                print(f"PASS {name} [{context}] side effects preserved, later call skipped")

    for name, prelude, body, reason in REFUSED:
        text = prelude + body + "\n"
        got = run(nift, text, vm=True)
        if got[2] == 0 or "not compiled for the VM" not in got[1] or reason not in got[1]:
            failed += 1
            print(f"FAIL {name}: expected refusal {reason}, got rc={got[2]} err={got[1]!r}")
        else:
            refused += 1
            if args.verbose:
                print(f"REFUSED {name}: {reason}")

    print(f"PASS {passed}, FAIL {failed}, REFUSED {refused}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
