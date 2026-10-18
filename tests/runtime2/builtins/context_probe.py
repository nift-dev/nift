#!/usr/bin/env python3
"""CP14 built-ins: legacy behaviour of collection and string built-ins in every
evaluation context (D-018 evidence, D-015 candidates).

Each case is (setup, expression). The expression is evaluated by the legacy
binary in six contexts:

  top        `r := EXPR` at top level (canonical evaluator)
  top-arg    `print(type(EXPR))` / `print([EXPR].stringify())` at top level
  loop       `r := EXPR` inside a `while` body (prepared evaluator)
  loop-arg   the print-argument form inside a `while` body
  fn         `r := EXPR` in a function called from top level
  fn-loop    the same function called from a `while` body

and prints `type(r)` (declaration forms) and `[r].stringify()` (or the diagnostic message without
its position). A case whose outcome differs between contexts is a
context-dependent behaviour: Runtime 2.0 gives it one meaning (D-018), so the
VM must not reproduce the difference; it is reported here and recorded in
docs/runtime-2/checkpoints/cp14-builtins-design.md as a D-015 candidate.

Usage: context_probe.py [--nift PATH] [--json OUT] [--only SUBSTRING]
Exit status 0 always unless --expect-file is given: then the set of
context-dependent case ids must equal the file's list (pinning the inventory).
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from probe_cases import CASES, PAIRS  # noqa: E402

CONTEXTS = ("top", "top-arg", "loop", "loop-arg", "fn", "fn-loop")


def indent(lines, pad):
    return "\n".join(pad + line for line in lines)


def body_decl(expr):
    return ["r := " + expr, "print(type(r))", "print([r].stringify())"]


def body_arg(expr):
    # One evaluation (mutating methods must not run twice); the type is
    # compared across the declaration contexts only.
    return ["print([" + expr + "].stringify())"]


def program(setup, expr, context):
    lines = list(setup)
    if context == "top":
        lines += body_decl(expr)
    elif context == "top-arg":
        lines += body_arg(expr)
    elif context in ("loop", "loop-arg"):
        body = body_decl(expr) if context == "loop" else body_arg(expr)
        lines += ["i := 0", "while(i < 1) {", indent(body, "  "), "  i += 1", "}"]
    elif context == "fn":
        lines = ["fn(f()) {", indent(body_decl(expr), "  "), "  return 0", "}"] + lines + ["f()"]
    elif context == "fn-loop":
        lines = ["fn(f()) {", indent(body_decl(expr), "  "), "  return 0", "}"] + lines
        lines += ["i := 0", "while(i < 1) {", "  f()", "  i += 1", "}"]
    return "\n".join(lines) + "\n"


POSITION = re.compile(r"^error: .*?:\d+:\d+: ")


def outcome(nift, workdir, source):
    path = os.path.join(workdir, "p.f")
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)
    env = dict(os.environ)
    for key in ("NIFT_VM", "NIFT_VM_STATS"):
        env.pop(key, None)
    proc = subprocess.run([nift, "p.f"], cwd=workdir, env=env, capture_output=True,
                          stdin=subprocess.DEVNULL, timeout=30)
    out = proc.stdout.decode("utf-8", "replace").strip("\n").split("\n")
    err = POSITION.sub("error: ", proc.stderr.decode("utf-8", "replace").strip())
    if proc.returncode == 0:
        return "ok " + " | ".join(out)
    return "error " + err


def comparable(value):
    """The value part of an outcome (`ok <type> | <json>` -> `ok <json>`)."""
    if value.startswith("ok ") and " | " in value:
        return "ok " + value.split(" | ", 1)[1]
    return value


def run(nift, cases, only):
    results = []
    with tempfile.TemporaryDirectory(prefix="cp14-probe-") as workdir:
        for case_id, setup, expr in cases:
            if only and only not in case_id:
                continue
            per = {ctx: outcome(nift, workdir, program(setup, expr, ctx)) for ctx in CONTEXTS}
            results.append({"id": case_id, "setup": setup, "expr": expr, "outcomes": per,
                            "context_dependent": len(set(map(comparable, per.values()))) > 1
                            or len(set(per[c] for c in CONTEXTS if "arg" not in c)) > 1})
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", default=os.environ.get("NIFT_LEGACY", "./nift"))
    parser.add_argument("--json")
    parser.add_argument("--only")
    parser.add_argument("--expect-file")
    args = parser.parse_args()
    results = run(args.nift, CASES, args.only)
    dependent = [r for r in results if r["context_dependent"]]
    for r in dependent:
        print("CONTEXT-DEPENDENT %s: %s" % (r["id"], r["expr"]))
        groups = {}
        for ctx, value in r["outcomes"].items():
            groups.setdefault(value, []).append(ctx)
        for value, ctxs in groups.items():
            print("    %-28s %s" % (",".join(ctxs), value[:160]))
    shape = []
    # Pairs of expressions that must mean the same (D-018: one meaning for the
    # same operation), compared at top level.
    with tempfile.TemporaryDirectory(prefix="cp14-pair-") as workdir:
        for pair_id, setup, left, right in PAIRS:
            if args.only and args.only not in pair_id:
                continue
            a = comparable(outcome(args.nift, workdir, program(setup, left, "top")))
            b = comparable(outcome(args.nift, workdir, program(setup, right, "top")))
            if a != b:
                print("SHAPE-DEPENDENT %s:\n    %-28s %s\n    %-28s %s" % (pair_id, "first", a[:120], "second", b[:120]))
                shape.append(pair_id)
    print("context_probe: %d cases x %d contexts, %d context-dependent; %d pairs, %d shape-dependent"
          % (len(results), len(CONTEXTS), len(dependent), len(PAIRS), len(shape)))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(results, handle, indent=1, ensure_ascii=False)
    if args.expect_file:
        with open(args.expect_file, encoding="utf-8") as handle:
            expected = sorted(line.strip() for line in handle if line.strip() and not line.startswith("#"))
        got = sorted([r["id"] for r in dependent] + shape)
        if expected != got:
            print("context_probe: FAIL: expected context-dependent %s, got %s" % (expected, got))
            return 1
        print("context_probe: PASS (inventory pinned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
