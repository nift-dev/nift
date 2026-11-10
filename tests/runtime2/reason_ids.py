#!/usr/bin/env python3
"""The renamed refusal reason ids of slot 1 (owner 2026-11-11 05:41, decisions.md D-068 #11).

The only intended user-visible change of slot 1: the reason id inside the refusal text. Everything
else in stderr and stdout stays the same. Exact before and after (the program's directory is shown
here as `./`):

  (b) `print("a" * 1)`, script
      before: error: ./main.f:1:7: not compiled for the VM (type:*): an operation whose operand types the VM cannot prove (*) is not supported by the Runtime 2.0 VM yet
      after:  error: ./main.f:1:7: not compiled for the VM (type:mul): an operation whose operand types the VM cannot prove (*) is not supported by the Runtime 2.0 VM yet
      session (`nift -i`), before: error: <stdin>:1:7: NIFT_VM session refused: type:*: an operation whose operand types the VM cannot prove (*) is not supported by the Runtime 2.0 VM yet
                           after:  error: <stdin>:1:7: NIFT_VM session refused: type:mul: an operation whose operand types the VM cannot prove (*) is not supported by the Runtime 2.0 VM yet
  (c) `x := async(f)`
      before: error: ./main.f:2:6: not compiled for the VM (async(callable, ...) was removed; declare an async function/lambda and call it directly): a language feature is not supported by the Runtime 2.0 VM yet
      after:  error: ./main.f:2:6: not compiled for the VM (removed-builtin:async): a language feature is not supported by the Runtime 2.0 VM yet
      `x := await(f())`
      before: error: ./main.f:2:6: not compiled for the VM (await(...) was removed; use 'await future' or 'await async_fn(args)'): a language feature is not supported by the Runtime 2.0 VM yet
      after:  error: ./main.f:2:6: not compiled for the VM (removed-builtin:await): a language feature is not supported by the Runtime 2.0 VM yet

The three note lines after the first line, stdout (empty) and the exit status (1) are unchanged, and
NIFT_VM_STATS records the new id. Legacy's own messages are not touched.

--mutant type-mul|removed-builtin: the binary is a negative-control build that restores the old ids;
the suite must fail on exactly the named group of cases.

usage: reason_ids.py --nift NIFT [--mutant type-mul|removed-builtin]
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

TYPE_TEXT = "an operation whose operand types the VM cannot prove (*) is not supported by the Runtime 2.0 VM yet"
FEATURE_TEXT = "a language feature is not supported by the Runtime 2.0 VM yet"
NOTES = ["note: the program was not executed: no part of it ran",
         "note: to run it on the legacy interpreter: NIFT_VM=0 nift main.f",
         "note: please report unsupported programs at https://github.com/nift-dev/nift/issues"]

SCRIPTS = [
    # (case, group, main.f, expected reason id, expected first stderr line after "error: <dir>/")
    ("type-mul-script", "type-mul", 'print("a" * 1)\n', "type:mul",
     "main.f:1:7: not compiled for the VM (type:mul): " + TYPE_TEXT),
    ("removed-builtin-async", "removed-builtin", "fn(f()) { return 1 }\nx := async(f)\n", "removed-builtin:async",
     "main.f:2:6: not compiled for the VM (removed-builtin:async): " + FEATURE_TEXT),
    ("removed-builtin-await", "removed-builtin", "fn[async](f()) { return 1 }\nx := await(f())\n",
     "removed-builtin:await", "main.f:2:6: not compiled for the VM (removed-builtin:await): " + FEATURE_TEXT),
]
OLD_IDS = ["(type:*)", "async(callable, ...) was removed", "await(...) was removed"]


def env_for(d, stats=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(HOME=d, LANG="C")
    if stats:
        env["NIFT_VM_STATS"] = stats
    return env


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--mutant", choices=["type-mul", "removed-builtin"])
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    failures = []  # (case, group, why)
    for case, group, src, reason, first in SCRIPTS:
        with tempfile.TemporaryDirectory(prefix="reason-ids-") as d:
            with open(os.path.join(d, "main.f"), "w") as f:
                f.write(src)
            stats = os.path.join(d, "stats.jsonl")
            p = subprocess.run([nift, "main.f"], cwd=d, env=env_for(d, stats), capture_output=True, text=True,
                               timeout=60)
            lines = p.stderr.replace(d + "/", "").rstrip("\n").split("\n")
            want = ["error: " + first] + NOTES
            if p.returncode != 1 or p.stdout != "" or lines != want:
                failures.append((case, group, "exit %d stdout %r stderr %r, want %r" % (p.returncode, p.stdout,
                                                                                         lines, want)))
            if any(old in p.stderr for old in OLD_IDS):
                failures.append((case + "-old-id", group, "an old reason id is still shown: %r" % p.stderr[:300]))
            seen = []
            if os.path.exists(stats):
                for line in open(stats):
                    try:
                        seen.append(json.loads(line).get("reason", ""))
                    except ValueError:
                        pass
            if reason not in seen:
                failures.append((case + "-stats", group, "NIFT_VM_STATS reasons %r, want %r" % (seen, reason)))
    with tempfile.TemporaryDirectory(prefix="reason-ids-") as d:
        p = subprocess.run([nift, "-i"], cwd=d, env=env_for(d), input='print("a" * 1)\n', capture_output=True,
                           text=True, timeout=60)
        first = p.stderr.split("\n")[0]
        want = "error: <stdin>:1:7: NIFT_VM session refused: type:mul: " + TYPE_TEXT
        if first != want or p.stdout != "":
            failures.append(("type-mul-session", "type-mul", "first line %r stdout %r, want %r" %
                             (first, p.stdout, want)))
    if a.mutant is None:
        for case, _, why in failures:
            print("FAIL reason ids: %s: %s" % (case, why))
        print("%s reason ids: type:mul, removed-builtin:async, removed-builtin:await (exact refusal texts)" %
              ("FAIL" if failures else "PASS"))
        return 1 if failures else 0
    groups = sorted({g for _, g, _ in failures})
    if groups != [a.mutant]:
        print("FAIL reason ids mutant %s: expected failures only in group %s, got %s (%s)" %
              (a.mutant, a.mutant, groups or "none", [c for c, _, _ in failures]))
        return 1
    print("PASS reason ids mutant %s: caught by %s" % (a.mutant, ", ".join(sorted(c for c, _, _ in failures))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
