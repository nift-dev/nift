#!/usr/bin/env python3
"""Runtime 2.0 CP12: the D-018 approved differences, old and new side by side.

  vm_d018.py --nift ./nift [--show]

D-018 (owner decision, CP12): one compiled representation per function and
one meaning for every expression wherever it appears. Three legacy
behaviours that depended on the calling context change (approved):

  D-018a  exactly-once evaluation and one number rule: legacy computes loop
          arithmetic in doubles and re-evaluates a loop statement whose value
          reaches 2^53 (its calls run twice); the VM evaluates every statement
          once with the canonical (CP09) number rule everywhere.
  D-018b  print(path) with a missing element evaluates its keys once (legacy's
          loop evaluator resolved the path a second time). Not observable in
          the compiled subset: these programs must stay identical.
  D-018c  errors are reported where they occur: at the failing expression,
          with the canonical wording (D-009#1) and without "return: "
          prefixes (D-009#5), in every context (legacy: the enclosing `if`,
          `return` or index; in loop code the statement, the outermost loop
          statement or the caller's call site, and loop wording such as
          "invalid index").

Each live case runs legacy (old) and NIFT_VM=1 (new), checks both behaviours
against the expected text, and checks that vm_policies.classify approves the
difference with exactly the named policy (or finds no difference). The
synthetic cases check that each policy refuses the same kind of difference
without its evidence or outside its shape. Exit 1 on any failure.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vm_policies  # noqa: E402

W = 9007199254740991  # Python ints: the pinned (D-018a) VM results below

# (name, source, policy, "pinned" (the VM result is checked exactly, legacy differs, no policy approves) or None
# for identical, [(engine, channel, expected substring), ...])
LIVE = [
    ("numbers-reevaluation",
     'fn(loud(x)) { print("loud " + x)\nreturn x }\nw := 9007199254740991\ni := 0\n'
     'while (i < 1) { t := loud(w + i) + 2\nprint(t)\ni += 1 }\n', "pinned",
     [("legacy", "out", "loud 9007199254740991\nloud 9007199254740991\n9007199254740993\n"),
      ("vm", "out=", "loud %d\n%d\n" % (W, W + 2)), ("vm", "err=", "")]),
    ("numbers-loop-double",
     "w := 9007199254740991\ni := 0\nwhile (i < 1) { i += 1\nprint(w + 2) }\nprint(w + 2)\n", "pinned",
     [("legacy", "out", "9007199254740992\n9007199254740993\n"),
      ("vm", "out=", "%d\n%d\n" % (W + 2, W + 2)), ("vm", "err=", "")]),
    ("numbers-overflow-in-loop-call",
     "fn(cube(b)) { return b * b * b }\ni := 0\nwhile (i < 1) { i += 1\nprint(cube(4000000)) }\n", "pinned",
     # 4000000^3 = 6.4e19 > 2^63 - 1: (b * b) * b fails; it starts at column 22
     [("legacy", "out", "6.4e+19\n"), ("vm", "out=", ""),
      ("vm", "err", ":1:22: signed 64-bit integer overflow\n")]),
    # The same function called directly: legacy's canonical overflow too
    # (reported at the `return`, prefixed); the VM's error is the same as in
    # the loop.
    ("numbers-overflow-top-level", "fn(cube(b)) { return b * b * b }\nprint(cube(4000000))\n", "D-018c diagnostics",
     [("legacy", "err", ":1:15: return: signed 64-bit integer overflow"),
      ("vm", "err", ":1:22: signed 64-bit integer overflow")]),
    ("diagnostics-return-prefix", "fn(f(a)) { return a[2] }\nprint(f([1]))\n", "D-018c diagnostics",
     [("legacy", "err", ":1:12: return: JSON array index 2 is out of range in 'a[2]'"),
      ("vm", "err", ":1:19: JSON array index 2 is out of range in 'a[2]'")]),
    ("diagnostics-if", "x := 0\nif (1 / x > 0) { print(1) }\n", "D-018c diagnostics",
     [("legacy", "err", ":2:1: division by zero"), ("vm", "err", ":2:5: division by zero")]),
    ("diagnostics-loop-index",  # g(a, 2): legacy's loop wording omits the index a parameter carries: pinned
     "fn(g(a, k)) { return a[k] }\na := [1, 2]\nk := 0\nwhile (k < 4) { print(g(a, k))\nk += 1 }\n",
     "pinned",
     [("legacy", "err", "invalid index"), ("vm", "out=", "1\n2\n"),
      ("vm", "err", ":1:22: JSON array index 2 is out of range in 'a[k]'\n")]),
    ("diagnostics-nested-loop-statement",
     "a := [1]\ni := 0\nwhile (i < 1) { i += 1\nj := 0\nwhile (j < 1) { j += 1\na[3] = 1 } }\n",
     "D-018c diagnostics",
     [("legacy", "err", "invalid index target"), ("vm", "err", ":6:1: index out of range: 3")]),
    ("diagnostics-direct-call",  # the same function outside loops: legacy and VM agree
     'fn(f(a)) {\nx := 1\nx = a\nreturn x }\nprint(f("s"))\n', None,
     [("vm", "err", ":3:1: cannot assign string to int binding 'x'")]),
    ("print-path-index", "a := [1]\nk := 3\ni := 0\nwhile (i < 1) { i += 1\nprint(a[k]) }\n", None,
     [("vm", "err", "JSON array index 3 is out of range in 'a[k]'")]),
    ("print-path-key-call",
     'fn(key(x)) { print("key " + x)\nreturn x }\na := [1]\ni := 0\nwhile (i < 1) { i += 1\nprint(a[key(3)]) }\n',
     None, [("vm", "out", "key 3\n")]),
    ("print-path-member", 'o := {"k": 1}\ni := 0\nwhile (i < 1) { i += 1\nprint((o.z)) }\n', None,
     [("vm", "err", "value has no member: z")]),
]

DIAG = b"error: /x/p.f:%d:%d: %s\n"
SRC_DIV = b"x := 0\nif (1 / x > 0) { print(1) }\n"   # legacy reports the `if` (2:1), the VM the division (2:5)
SRC_IDX = b"a := [1]\na[2]\n"


def diag(line, col, msg):
    return DIAG % (line, col, msg)


# (name, legacy, vm, stats, expected verdict, expected policy[, source])
SYNTHETIC = [
    ("diagnostics-approved", (b"a\n", diag(2, 1, b"return: division by zero"), 1),
     (b"a\n", diag(2, 5, b"division by zero"), 1), {"outcome": "vm-error", "line": 2, "column": 5},
     "approved", "D-018c diagnostics", SRC_DIV),
    ("diagnostics-vm-site-not-the-division", (b"a\n", diag(2, 1, b"return: division by zero"), 1),
     (b"a\n", diag(2, 1, b"division by zero"), 1), {"outcome": "vm-error", "line": 2, "column": 1},
     "diff", None, SRC_DIV),
    ("diagnostics-vm-site-after-the-division", (b"a\n", diag(2, 1, b"return: division by zero"), 1),
     (b"a\n", diag(2, 13, b"division by zero"), 1), {"outcome": "vm-error", "line": 2, "column": 13},
     "diff", None, SRC_DIV),
    ("diagnostics-other-stdout", (b"a\n", diag(2, 1, b"division by zero"), 1),
     (b"b\n", diag(2, 8, b"division by zero"), 1), {"outcome": "vm-error"}, "diff", None),
    ("diagnostics-other-message", (b"", diag(2, 1, b"division by zero"), 1),
     (b"", diag(2, 8, b"modulo by zero"), 1), {"outcome": "vm-error", "error_in_loop_path": True}, "diff", None),
    ("diagnostics-loop-wording-no-vm-evidence-needed", (b"", diag(2, 1, b"invalid index"), 1),
     (b"", diag(2, 1, b"JSON array index 2 is out of range in 'a[2]'"), 1), {"outcome": "vm-error", "line": 2,
                                                                           "column": 1}, "approved",
     "D-018c diagnostics", SRC_IDX),
    ("diagnostics-loop-wording-wrong-index", (b"", diag(2, 1, b"invalid index"), 1),
     (b"", diag(2, 1, b"JSON array index 3 is out of range in 'a[2]'"), 1), {"outcome": "vm-error", "line": 2,
                                                                           "column": 1}, "diff", None, SRC_IDX),
    ("diagnostics-loop-wording-with-evidence", (b"", diag(2, 1, b"invalid index"), 1),
     (b"", diag(2, 1, b"JSON array index 2 is out of range in 'a[2]'"), 1),
     {"outcome": "vm-error", "error_in_loop_path": True, "line": 2, "column": 1}, "approved", "D-018c diagnostics",
     SRC_IDX),
    ("diagnostics-binding-name-changed", (b"", diag(2, 1, b"cannot change binding type: x"), 1),
     (b"", diag(2, 1, b"cannot assign string to int binding 'y'"), 1),
     {"outcome": "vm-error", "error_in_loop_path": True}, "diff", None),
    ("diagnostics-earlier-position", (b"", diag(3, 5, b"division by zero"), 1),
     (b"", diag(3, 1, b"division by zero"), 1), {"outcome": "vm-error"}, "diff", None),
    ("diagnostics-vm-completed", (b"", diag(3, 5, b"division by zero"), 1), (b"", b"", 0), {"outcome": "vm"},
     "diff", None),
    ("diagnostics-fallback", (b"", diag(2, 1, b"x"), 1), (b"", diag(2, 2, b"x"), 1), {"outcome": "fallback"},
     "diff", None),
    ("numbers-approved", (b"9007199254740992\n", b"", 0), (b"9007199254740993\n", b"", 0),
     {"outcome": "vm", "large_in_loop_path": 1, "large_lines": [0]}, "diff", None),  # no D-018a policy (round 2)
    ("numbers-unattributed-line", (b"9007199254740992\n", b"", 0), (b"9007199254740993\n", b"", 0),
     {"outcome": "vm", "large_in_loop_path": 1, "large_lines": [1]}, "diff", None),
    ("numbers-without-evidence", (b"9007199254740992\n", b"", 0), (b"9007199254740993\n", b"", 0),
     {"outcome": "vm", "large_numbers": 1, "large_in_loop_path": 0}, "diff", None),
    ("numbers-text-changed", (b"value 1\n", b"", 0), (b"other 1\n", b"", 0),
     {"outcome": "vm", "large_in_loop_path": 1}, "diff", None),
    ("numbers-extra-vm-line", (b"1\n", b"", 0), (b"1\nnew\n", b"", 0),
     {"outcome": "vm", "large_in_loop_path": 1}, "diff", None),
    ("numbers-legacy-extra-unrepeated-line", (b"a\nb\n", b"", 0), (b"a\n", b"", 0),
     {"outcome": "vm", "large_in_loop_path": 1}, "diff", None),
    ("numbers-other-vm-error", (b"1\n", b"", 0), (b"", diag(1, 1, b"division by zero"), 1),
     {"outcome": "vm-error", "large_in_loop_path": 1}, "diff", None),
    ("numbers-overflow-stop", (b"a\n6.4e+19\nb\n", b"", 0), (b"a\n", diag(1, 1, b"signed 64-bit integer overflow"), 1),
     {"outcome": "vm-error", "large_in_loop_path": 1, "error_in_loop_path": True, "large_lines": [1], "line": 1,
      "column": 1}, "diff", None),
    ("numbers-overflow-stop-unattributed", (b"a\n6.4e+19\nb\n", b"", 0),
     (b"a\n", diag(1, 1, b"signed 64-bit integer overflow"), 1),
     {"outcome": "vm-error", "large_in_loop_path": 1, "error_in_loop_path": True, "large_lines": [0], "line": 1,
      "column": 1}, "diff", None),
    ("recursion-approved", (b"0\n", diag(1, 1, b"callable recursion depth exceeded: d"), 1), (b"0\n1\n", b"", 0),
     {"outcome": "vm", "beyond_legacy_depth": True}, "approved", "D-001#4 recursion"),
    ("recursion-without-evidence", (b"0\n", diag(1, 1, b"callable recursion depth exceeded: d"), 1),
     (b"0\n1\n", b"", 0), {"outcome": "vm"}, "diff", None),
]


def run(nift, d, mode):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "HOME": d, "TMPDIR": d}
    for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
        if k in os.environ:
            env[k] = os.environ[k]
    stats = os.path.join(d, ".stats")
    if os.path.exists(stats):
        os.unlink(stats)
    if mode:
        env.update({"NIFT_VM": mode, "NIFT_VM_STATS": stats})
    p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL, timeout=60)
    st = None
    if mode and os.path.exists(stats):
        import json
        st = json.loads(open(stats).read().splitlines()[-1])
    return (p.stdout, p.stderr, p.returncode), st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--show", action="store_true", help="print old and new behaviour of every live case")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = []
    work = tempfile.mkdtemp(prefix="nift-vm-d018-")
    try:
        for name, source, policy, expects in LIVE:
            d = os.path.join(work, name)
            os.makedirs(d)
            with open(os.path.join(d, "p.f"), "w") as f:
                f.write(source)
            legacy, _ = run(nift, d, None)
            vm, st = run(nift, d, "1")
            if not st or st["outcome"] not in ("vm", "vm-error") or st.get("vm_executions") != 1:
                failures.append(f"{name}: did not run on the VM ({st and st['outcome']} {st and st.get('reason')})")
                continue
            verdict, got = vm_policies.classify(legacy, vm, st)
            want = ("diff", None) if policy == "pinned" else ("approved", policy) if policy else ("same", None)
            if (verdict, got) != want:
                failures.append(f"{name}: classified {verdict} {got}, expected {want[0]} {want[1]}")
            for engine, channel, text in expects:
                result = legacy if engine == "legacy" else vm
                data = (result[0] if channel.startswith("out") else result[1]).decode()
                if (data != text) if channel.endswith("=") else (text not in data):
                    failures.append(f"{name}: {engine} {channel} lacks {text!r}: {data[-300:]!r}")
            if args.show or any(f.startswith(name + ":") for f in failures):
                print(f"--- {name} [{policy or 'identical'}]")
                print("    " + source.replace("\n", "\n    ").rstrip())
                for label, r in (("old (legacy)", legacy), ("new (VM)", vm)):
                    err = r[1].decode().replace(d, "$D")
                    print(f"  {label}: exit {r[2]} stdout {r[0].decode()!r} stderr {err!r}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for name, legacy, vm, st, verdict, policy, *source in SYNTHETIC:
        got = vm_policies.classify(legacy, vm, st, source[0] if source else None)
        if got != (verdict, policy):
            failures.append(f"synthetic {name}: {got}, expected ({verdict}, {policy})")
    for f in failures:
        print("FAIL " + f)
    print(("PASS" if not failures else "FAIL") + f" vm D-018: {len(LIVE)} live cases (old vs new), "
          f"{len(SYNTHETIC)} policy boundary cases, {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
