#!/usr/bin/env python3
"""C49: the precondition that makes B4b mutant A29 equivalent (combined-b3-b5.md section 39).

A29 removes the exposure of a value a session statement stores into an earlier value that has no
site (src/rt/infer_sites.cpp, Inference::site_store, the `session_ && !has_site(container)`
branch). In sessions that branch is reached only by stores into an earlier non-aggregate value
(int, bool, string, null, a lambda; and, from the review of C47-C49, a double, a declared fn used
as a value, a mutex, a channel, a set, an error) or an earlier struct instance. Every such statement is refused
for its receiver's kind alone, unmutated and mutated alike, inside a dead branch too, so the
dropped exposure is never observable and A29 is retired from tests/runtime2/b4b/mutants.sh.

This check pins those refusals. If any of these statements is ever admitted, the exposure becomes
observable again: restore A29 in mutants.sh (and its proof no longer holds).

usage: a29_precondition.py --nift NIFT
"""
import argparse
import os
import subprocess
import sys
import tempfile

EARLIER = {
    "int": "x := 1",
    "bool": "x := true",
    "string": 'x := "s"',
    "null": "x := null",
    "lambda": "x := () => 1",
    "struct": "struct(S) { a := 1 }\nx := S()",
    # Review of C47-C49 (combined-b3-b5.md section 44): line 298 is also reached from these.
    "double": "x := 1.5",
    "fn value": "fn(h()) { return 1 }\nx := h",
    "mutex": "x := mutex()",
    "channel": "x := channel(1)",
    "set": "x := set()",
    "error": 'x := error("e")',
}
# A receiver kind with its own refusal reason for a store (a set has no push method).
OVERRIDE = {("set", "push"): "method:push"}
STORES = {  # store -> (non-struct reason, struct reason)
    "push": ("x.push(ys)", "type:push", "type:push"),
    "index": ("x[0] = ys", "type:element-assignment", "type:element-assignment"),
    "key": ('x["k"] = ys', "index-target", "index-target"),
    "member": ("x.k = ys", "static-error:member-assign", "struct-no-field"),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    fails = count = 0
    with tempfile.TemporaryDirectory(prefix="a29-precondition-") as work:
        for kind, decl in EARLIER.items():
            for store, (text, plain, struct) in STORES.items():
                want = OVERRIDE.get((kind, store), struct if kind == "struct" else plain)
                for guarded in (False, True):
                    body = f"if (false) {{ {text} }}" if guarded else text
                    stdin = f'{decl}\nys := [" a "]; {body}; print(ys[0].trim())\nprint("end")\n'
                    p = subprocess.run([nift, "-i"], cwd=work, input=stdin, capture_output=True, text=True,
                                       env=dict(os.environ, NIFT_VM="require"), timeout=60)
                    count += 1
                    refused = f"NIFT_VM session refused: {want}" in p.stderr
                    ran = "a" in p.stdout.split()  # the statement's own print never ran
                    label = f"{kind} {store}{' (dead branch)' if guarded else ''}"
                    if refused and not ran:
                        print(f"PASS a29 precondition: {label}: refused {want}")
                    else:
                        fails += 1
                        print(f"FAIL a29 precondition: {label}: not refused for {want} "
                              f"(stdout {p.stdout!r}, stderr {p.stderr[:200]!r}): restore mutant A29")
    print(("FAIL" if fails else "PASS") + f" a29 precondition: {count} session statements, {fails} admitted")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
