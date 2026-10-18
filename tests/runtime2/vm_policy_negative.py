#!/usr/bin/env python3
"""Runtime 2.0 CP12: the approved-difference policies reject real bugs.

  vm_policy_negative.py

Review CP12 round 1 (mask.py) showed the first D-018a/D-018c policies
approving synthetic VM bugs whenever some evidence existed anywhere in the
run: an unrelated 3.5 -> 3, changed loop counters, dropped iterations, a
missing final line, `inf` matched inside words, a numeric stop masking later
lines, wrong error kinds and positions. This suite pins the narrowed
policies (vm_policies.py): every synthetic bug below must be classified
'diff' even though the statistics carry the evidence a genuine run of the
same shape would have; the positive controls (genuine D-018 shapes) must
stay approved. No binary needed; exit 1 on any failure.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vm_policies  # noqa: E402

A, C = "approved", "diff"
NUM, DIAG, REC = "D-018a numbers", "D-018c diagnostics", "D-001#4 recursion"


def d(line, col, msg):
    return b"error: /x/p.f:%d:%d: %s\n" % (line, col, msg)


def st(**kw):
    base = {"outcome": "vm", "large_in_loop_path": 1, "large_lines": [], "derived_lines": [], "line": 0, "column": 0}
    base.update(kw)
    return base


# A loop printing a large value and a counter, then an unrelated result:
#   while (i < 3) { print(w + 2); print(i); i++ }  print(7 / 2)
LEG = b"9007199254740992\n0\n9007199254740992\n1\n9007199254740992\n2\n3.5\n"
VM = b"9007199254740993\n0\n9007199254740993\n1\n9007199254740993\n2\n3.5\n"
EVID = st(large_in_loop_path=3, large_lines=[0, 2, 4])  # the lines `w + 2` is printed on

# Re-evaluation: `t := loud(w + i) + 2` prints "loud" twice on legacy.
RE_LEG, RE_VM = b"before\nloud\nloud\n9007199254740993\n7\n", b"before\nloud\n9007199254740993\n7\n"
RE_EVID = st(large_lines=[2])

# Numeric stop: the VM stops with an overflow in loop code where legacy went on in doubles.
STOP_SRC = b"print(\"before\")\ni := 0\nwhile (i < 1) { i += 1\nprint(cube(4000000)) }\n"
STOP_VM = (b"before\n", d(4, 7, b"signed 64-bit integer overflow"), 1)
STOP_EVID = st(outcome="vm-error", error_in_loop_path=True, large_lines=[1], line=4, column=7)

# Diagnostics sources.
SRC_RET = b"fn(f(a)) { return a[2] }\nprint(f([1]))\n"
SRC_LOOP = (b"fn(g(a, k)) {\nreturn a[k]\n}\na := [1, 2]\nk := 0\nwhile (k < 4) { print(g(a, k))\nk += 1 }\n"
            b"x := 1\n\n\n\nprint(x)\n")
SRC_BIND = b"fn(f(a)) {\nx := 1\nx = a\nreturn x }\ni := 0\nwhile (i < 1) { i += 1\nprint(f(\"s\")) }\n"
LOOP_ERR = st(outcome="vm-error", error_in_loop_path=True, large_in_loop_path=0, line=2, column=8)

CASES = [
    # --- positive controls (genuine shapes) ---
    ("numbers: large values on attributed lines (no policy: pinned in vm_golden.py)", (LEG, b"", 0), (VM, b"", 0), EVID, None, C, None),
    ("numbers: re-evaluated call printed twice (no policy: pinned in vm_golden.py)", (RE_LEG, b"", 0), (RE_VM, b"", 0), RE_EVID, None, C, None),
    ("numbers: small consequence of a large operand (no policy: pinned in vm_golden.py)", (b"0\n", b"", 0), (b"1\n", b"", 0),
     st(large_lines=[0], derived_lines=[0]), None, C, None),
    ("numbers: boolean consequence of a large operand (no policy: pinned in vm_golden.py)", (b"false\n", b"", 0), (b"true\n", b"", 0),
     st(large_lines=[0], derived_lines=[0]), None, C, None),
    ("numbers: overflow stop, legacy went on (no policy: pinned in vm_golden.py)", (b"before\n6.4e+19\nafter\n", b"", 0), STOP_VM, STOP_EVID,
     STOP_SRC, C, None),
    ("numbers: overflow stop, legacy failed later (no policy: pinned in vm_golden.py)", (b"before\n6.4e+19\n", d(9, 1, b"invalid index"), 1), STOP_VM,
     STOP_EVID, STOP_SRC, C, None),
    ("diagnostics: return prefix, same line", (b"", d(1, 12, b"return: JSON array index 2 is out of range in 'a[2]'"), 1),
     (b"", d(1, 19, b"JSON array index 2 is out of range in 'a[2]'"), 1),
     st(outcome="vm-error", large_in_loop_path=0, line=1, column=19), SRC_RET, A, DIAG),
    ("diagnostics: loop wording, index carried by a parameter (unverifiable)", (b"", d(6, 23, b"invalid index"), 1),
     (b"", d(2, 8, b"JSON array index 2 is out of range in 'a[k]'"), 1), LOOP_ERR, SRC_LOOP, C, None),
    ("diagnostics: loop binding wording (refused at compile time instead)", (b"", d(7, 7, b"cannot change binding type: x"), 1),
     (b"", d(3, 1, b"cannot assign string to int binding 'x'"), 1),
     st(outcome="vm-error", error_in_loop_path=True, large_in_loop_path=0, line=3, column=1), SRC_BIND, C, None),
    ("recursion beyond legacy's depth", (b"0\n", d(1, 1, b"callable recursion depth exceeded: d"), 1),
     (b"0\n1\n", b"", 0), st(beyond_legacy_depth=True, large_in_loop_path=0), None, A, REC),

    # --- synthetic VM bugs with the evidence a genuine run would carry: all rejected ---
    ("unrelated result 3.5 -> 3", (LEG, b"", 0), (VM[:-4] + b"3\n", b"", 0), EVID, None, C, None),
    ("loop counters changed", (LEG, b"", 0), (VM.replace(b"\n0\n", b"\n5\n").replace(b"\n1\n", b"\n5\n"), b"", 0),
     EVID, None, C, None),
    ("dropped last iteration", (LEG, b"", 0), (b"9007199254740993\n0\n9007199254740993\n1\n3.5\n", b"", 0), EVID, None,
     C, None),
    ("dropped two iterations", (LEG, b"", 0), (b"9007199254740993\n0\n3.5\n", b"", 0), EVID, None, C, None),
    ("missing final line", (LEG, b"", 0), (VM[:-4], b"", 0), EVID, None, C, None),
    ("sign flip -3.5", (LEG, b"", 0), (VM[:-4] + b"-3.5\n", b"", 0), EVID, None, C, None),
    ("inf instead of a number on an unattributed line", (LEG, b"", 0), (VM[:-4] + b"inf\n", b"", 0), EVID, None, C,
     None),
    ("extra VM line", (LEG, b"", 0), (VM + b"extra\n", b"", 0), EVID, None, C, None),
    ("small number changed beside a large one", (b"n 9007199254740992 i 0\n", b"", 0),
     (b"n 9007199254740993 i 1\n", b"", 0), st(large_lines=[0]), None, C, None),
    ("boolean flipped without a large operand", (b"9007199254740992 false\n", b"", 0),
     (b"9007199254740993 true\n", b"", 0), st(large_lines=[0]), None, C, None),
    ("digits inside words", (b"user42 info\n", b"", 0), (b"user43 1o\n", b"", 0), st(large_lines=[0], derived_lines=[0]),
     None, C, None),
    ("inf inside a word", (b"info\n", b"", 0), (b"9007199254740993o\n", b"", 0), st(large_lines=[0], derived_lines=[0]),
     None, C, None),
    ("text changed on an attributed line", (b"value 9007199254740992\n", b"", 0), (b"other 9007199254740993\n", b"", 0),
     st(large_lines=[0]), None, C, None),
    ("legacy line deleted, not a repeat", (b"a\nb\nc\n", b"", 0), (b"a\nc\n", b"", 0), st(large_lines=[0, 1, 2]), None,
     C, None),
    ("repeat deleted where no event is attributed", (RE_LEG, b"", 0), (RE_VM, b"", 0), st(large_lines=[3]), None, C,
     None),
    ("numbers changed without per-line evidence", (LEG, b"", 0), (VM, b"", 0), st(large_in_loop_path=3), None, C,
     None),
    ("stop not attributed to a large-number event", (b"before\n6.4e+19\nafter\n", b"", 0), STOP_VM,
     dict(STOP_EVID, large_lines=[0]), STOP_SRC, C, None),
    ("stop outside loop-evaluator code", (b"before\n6.4e+19\nafter\n", b"", 0), STOP_VM,
     dict(STOP_EVID, error_in_loop_path=False), STOP_SRC, C, None),
    ("stop located elsewhere by the statistics", (b"before\n6.4e+19\nafter\n", b"", 0), STOP_VM,
     dict(STOP_EVID, line=2, column=1), STOP_SRC, C, None),
    ("non-numeric VM stop masking legacy's lines", (b"before\n6.4e+19\nafter\n", b"", 0),
     (b"before\n", d(4, 7, b"division by zero"), 1), dict(STOP_EVID, reason="division by zero"), STOP_SRC, C, None),
    ("legacy other error, VM overflow (mask.py)", (b"a\n", d(9, 1, b"division by zero"), 1),
     (b"", d(2, 1, b"signed 64-bit integer overflow"), 1), {"outcome": "vm-error", "large_in_loop_path": 1}, None, C,
     None),
    ("long legacy output, early VM overflow (mask.py)", (b"1\n2\n3\nDONE\n", b"", 0),
     (b"", d(2, 1, b"signed 64-bit integer overflow"), 1), {"outcome": "vm-error", "large_in_loop_path": 1}, None, C,
     None),
    ("diagnostics: loop error, legacy position not a loop or call site", (b"", d(8, 1, b"invalid index"), 1),
     (b"", d(2, 8, b"JSON array index 2 is out of range in 'a[k]'"), 1), LOOP_ERR, SRC_LOOP, C, None),
    ("diagnostics: VM position not the quoted expression", (b"", d(6, 23, b"invalid index"), 1),
     (b"", d(2, 8, b"JSON array index 2 is out of range in 'b[k]'"), 1), LOOP_ERR, SRC_LOOP, C, None),
    ("diagnostics: invalid index mapped to a member error", (b"", d(6, 23, b"invalid index"), 1),
     (b"", d(2, 8, b"value has no member: zzz"), 1), LOOP_ERR, SRC_LOOP, C, None),
    ("diagnostics: other error kind", (b"", d(6, 23, b"division by zero"), 1),
     (b"", d(2, 8, b"modulo by zero"), 1), LOOP_ERR, SRC_LOOP, C, None),
    ("diagnostics: later position on another line, not loop code", (b"", d(1, 12, b"return: division by zero"), 1),
     (b"", d(2, 7, b"division by zero"), 1), st(outcome="vm-error", line=2, column=7), b"x\nprint(1 / 0)\n", C, None),
    ("diagnostics: VM statistics are not consulted", (b"", d(1, 12, b"return: JSON array index 2 is out of range "
                                                              b"in 'a[2]'"), 1),
     (b"", d(1, 19, b"JSON array index 2 is out of range in 'a[2]'"), 1),
     st(outcome="vm-error", large_in_loop_path=0, line=1, column=12), SRC_RET, A, DIAG),
    ("diagnostics: binding name changed", (b"", d(7, 7, b"cannot change binding type: x"), 1),
     (b"", d(3, 1, b"cannot assign string to int binding 'y'"), 1),
     st(outcome="vm-error", error_in_loop_path=True, large_in_loop_path=0, line=3, column=1), SRC_BIND, C, None),
    ("diagnostics: positions differ and no source", (b"", d(1, 12, b"return: division by zero"), 1),
     (b"", d(1, 19, b"division by zero"), 1), st(outcome="vm-error", line=1, column=19, path="/nonexistent/p.f"),
     None, C, None),
    ("diag loop any position (mask.py)", (b"", d(40, 1, b"division by zero"), 1), (b"", d(2, 1, b"division by zero"), 1),
     {"outcome": "vm-error", "error_in_loop_path": True}, None, C, None),
    ("diag no loop, later pos far (mask.py)", (b"", d(2, 1, b"division by zero"), 1),
     (b"", d(90, 1, b"division by zero"), 1), {"outcome": "vm-error"}, None, C, None),
    ("diag invalid index -> any index msg (mask.py)", (b"", d(2, 1, b"invalid index"), 1),
     (b"", d(2, 1, b"JSON object has no key 'zzz'"), 1), {"outcome": "vm-error", "error_in_loop_path": True}, None, C,
     None),
    ("recursion without evidence", (b"0\n", d(1, 1, b"callable recursion depth exceeded: d"), 1), (b"0\n1\n", b"", 0),
     st(large_in_loop_path=0), None, C, None),
    ("fallback outcome", (b"", d(2, 1, b"x"), 1), (b"", d(2, 2, b"x"), 1), {"outcome": "fallback"}, None, C, None),
]

SRC_LOOPC = b"fn(g(a)) {\nreturn a[2]\n}\na := [1, 2]\nk := 0\nwhile (k < 4) { print(g(a))\nk += 1 }\n"
SRC_DIV2 = b"z := 0\nx := 4 / 2 + 1 / z\n"
SRC_MOD = b"x := 7\nm := 0 - 7\nprint(x % 3)\nprint(m % 3)\n"
MASK2 = [  # review round 2 (mask2.py): the evidence the *buggy* VM emits itself
    ("spurious overflow on small operands, self-attributed", (b"before\n3000\nafter\nDONE\n", b"", 0),
     (b"before\n", d(3, 7, b"signed 64-bit integer overflow"), 1),
     st(outcome="vm-error", error_in_loop_path=True, large_lines=[1], line=3, column=7),
     b"print(\"before\")\ni := 0\nwhile (i < 1) { i += 1\nprint(i * 3000) }\nprint(\"after\")\n", C, None),
    ("10 - 3 -> 13 on a derived line", (b"0 7\n", b"", 0), (b"0 13\n", b"", 0), st(large_lines=[0], derived_lines=[0]),
     None, C, None),
    ("2 + 2 -> 2^60, self-attested large result", (b"x 4\n", b"", 0), (b"x 1152921504606846976\n", b"", 0),
     st(large_lines=[0]), None, C, None),
    ("0.5 * 4 -> 1e+300", (b"2\n", b"", 0), (b"1e+300\n", b"", 0), st(large_lines=[0]), None, C, None),
    ("dropped one of three identical lines", (b"tick\ntick\ntick\nend\n", b"", 0), (b"tick\ntick\nend\n", b"", 0),
     st(large_lines=[0, 1, 2]), None, C, None),
    ("division by zero at the wrong '/' on the line", (b"", d(2, 1, b"division by zero"), 1),
     (b"", d(2, 6, b"division by zero"), 1), st(outcome="vm-error", large_in_loop_path=0, line=2, column=6), SRC_DIV2,
     C, None),
    ("division by zero at the right '/'", (b"", d(2, 1, b"division by zero"), 1),
     (b"", d(2, 14, b"division by zero"), 1), st(outcome="vm-error", large_in_loop_path=0, line=2, column=14), SRC_DIV2,
     A, DIAG),
    ("invalid index -> wrong index number", (b"", d(6, 23, b"invalid index"), 1),
     (b"", d(2, 8, b"JSON array index 7 is out of range in 'a[2]'"), 1), LOOP_ERR, SRC_LOOPC, C, None),
    ("invalid index -> right index number", (b"", d(6, 23, b"invalid index"), 1),
     (b"", d(2, 8, b"JSON array index 2 is out of range in 'a[2]'"), 1), LOOP_ERR, SRC_LOOPC, A, DIAG),
    ("binding message, right name, wrong types", (b"", d(7, 7, b"cannot change binding type: x"), 1),
     (b"", d(3, 1, b"cannot assign bool to array binding 'x'"), 1),
     st(outcome="vm-error", error_in_loop_path=True, large_in_loop_path=0, line=3, column=1), SRC_BIND, C, None),
    ("invalid index target -> other key text", (b"", d(1, 17, b"invalid index target"), 1),
     (b"", d(1, 17, b"index out of range: k"), 1),
     st(outcome="vm-error", error_in_loop_path=True, large_in_loop_path=0, line=1, column=17),
     b"while (i < 1) { a[k] = 1 }\n", C, None),
    ("Mod fast path sign", (b"1\n-1\n", b"", 0), (b"1\n1\n", b"", 0), st(large_in_loop_path=0), SRC_MOD, C, None),
]
CASES += MASK2


def main():
    failures = 0
    rejected = sum(1 for c in CASES if c[5] == C)
    for name, legacy, vm, stats, source, verdict, policy in CASES:
        got = vm_policies.classify(legacy, vm, stats, source)
        if got != (verdict, policy):
            failures += 1
            print(f"FAIL {name}: {got}, expected ({verdict}, {policy})")
    print(("PASS" if not failures else "FAIL") + f" vm policy negatives: {rejected} synthetic bugs rejected, "
          f"{len(CASES) - rejected} genuine shapes approved, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
