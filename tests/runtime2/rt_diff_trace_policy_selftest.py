#!/usr/bin/env python3
"""rt-diff: the D-001#4 recursion-budget class on the Host-trace channel
(owner, 2026-10-30). Legacy (A) stopped at its 64-frame limit; the VM (B)
went on. B's trace is approved only when A's events, in order and byte for
byte, are exactly B's first events. Every other trace change stays a
difference, and no other class approves the trace channel.

No binary needed; exit 1 on any failure.
"""
import importlib.machinery
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PATH = os.path.join(HERE, "..", "..", "tools", "rt-diff", "rt-diff")
loader = importlib.machinery.SourceFileLoader("rt_diff", PATH)
spec = importlib.util.spec_from_loader("rt_diff", loader)
rt = importlib.util.module_from_spec(spec)
loader.exec_module(rt)

RULE = {"id": "cp11-recursion-budget-class", "decision": "D-001#4", "class": "recursion-budget"}
E = [("script", '{"op":"stat","res":"file"}'), ("script", '{"op":"read","res":"ok"}'),
     ("script/thread#1", '{"op":"stat","res":"directory"}')]
EXTRA = ("script/thread#1", '{"op":"stat","res":"directory","arg":"x"}')


def run(trace_a, trace_b, recursion=True):
    a = {"stdout": b"1\n", "stderr": b"error: /x/p.f:2:3: callable recursion depth exceeded: r\n",
         "exit": [1], "combined": [], "trace": list(trace_a)}
    b = {"stdout": b"1\n2\n", "stderr": b"", "exit": [0], "combined": [], "trace": list(trace_b)}
    if not recursion:  # not the recursion-budget shape: A completed too
        a["stderr"], a["exit"] = b"", [0]
    diffs = [{"channel": "trace", "context": "script", "first": {}}]
    approved, remaining = rt.apply_policies({"id": "x", "path": __file__}, a, b, diffs, [RULE])
    return bool(approved) and not remaining


CASES = [
    ("legacy stopped before any Host operation", [], [E[2]], True, True),
    ("legacy trace is a prefix; VM continues", E[:2], E, True, True),
    ("common event modified", E[:2], [E[0], ("script", '{"op":"read","res":"err"}'), E[2]], True, False),
    ("common events reordered", E[:2], [E[1], E[0], E[2]], True, False),
    ("common event missing", E[:2], [E[0], E[2]], True, False),
    ("extra VM event before the divergence", E[:2], [E[0], EXTRA, E[1], E[2]], True, False),
    ("VM trace shorter than legacy", E, E[:2], True, False),
    ("not the recursion-budget shape", E[:2], E, False, False),
]


def main():
    failures = 0
    for name, ta, tb, recursion, want in CASES:
        got = run(ta, tb, recursion)
        if got != want:
            failures += 1
            print(f"FAIL {name}: approved={got}, expected {want}")
    print(("PASS" if not failures else "FAIL") + f" rt-diff recursion-budget trace policy: {len(CASES)} cases, "
          f"{sum(1 for c in CASES if not c[4])} rejected, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
