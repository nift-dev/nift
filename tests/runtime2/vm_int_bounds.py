#!/usr/bin/env python3
"""Runtime 2.0 CP12: exact integer arithmetic at the bounds, every int opcode.

  vm_int_bounds.py --nift ./nift [--jobs J]

Review round 2: the VM's large-integer results are checked against an
independent reference, Python's exact ints, never against the VM or legacy
(legacy computes loop code in doubles). Each program builds its operands from
small literals at top level (`q := 2147483648`, `h := q * q`, ...) and runs
one operation under NIFT_VM=require; the expected output is computed here:

  + - *          exact; outside [-2^63, 2^63 - 1] "signed 64-bit integer overflow"
  %              truncated (sign of the dividend), INT64_MIN % -1 = 0,
                 "modulo by zero"
  unary -        exact; -INT64_MIN = 9223372036854775808 (an exact Decimal)
  += -= *= %=    the same rules (AddAssign, Sub, Mul, Mod on the binding)
  += k, ++, --   AddI (small constants)

The error position is the failing node's first byte in the CP10 AST
(vm_sites.py). Operands: 0, +-1, +-3, +-7, 1000, 32767, +-(2^53 - 1),
+-2^53, 2^53 + 1, 2^62, 2^63 - 1, -2^63 + 1, -2^63.
"""
import argparse
import concurrent.futures as cf
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vm_sites  # noqa: E402

MIN, MAX = -(1 << 63), (1 << 63) - 1
VALUES = [0, 1, -1, 3, -3, 7, -7, 1000, 32767, (1 << 53) - 1, -((1 << 53) - 1), 1 << 53, -(1 << 53), (1 << 53) + 1,
          1 << 62, MAX, MIN + 1, MIN]
PRE = "q := 2147483648\nh := q * q\n"
OVERFLOW = "signed 64-bit integer overflow"


def build(v):
    """Expression text with value v, from literals below 2^31 (no overflow)."""
    if v == MIN:
        return "((0 - h) * 2)"
    if v < 0:
        return f"(0 - {build(-v)})"
    if v < (1 << 31):
        return str(v)
    if v >= 1 << 62:
        return f"(h + {build(v - (1 << 62))})"
    return f"(q * {build(v >> 31)} + {build(v & ((1 << 31) - 1))})"


def binary(op, a, b):
    """(value, None) or (None, message) for int64 `a op b`."""
    if op == "%":
        if b == 0:
            return None, "modulo by zero"
        r = abs(a) % abs(b)
        return (-r if a < 0 else r), None
    r = {"+": a + b, "-": a - b, "*": a * b}[op]
    return (r, None) if MIN <= r <= MAX else (None, OVERFLOW)


def pre(*values):
    """q and h only when an operand needs them: a large value switches the VM
    to its checked dispatch, so small operands must also run without one
    (the integer fast paths)."""
    return PRE if any(abs(v) >= 1 << 31 for v in values) else ""


def cases():
    for a in VALUES:
        decl = pre(a) + f"a := {build(a)}\n"
        yield f"neg {a}", decl + "print(-a)\n", (str(-a), None), None
        for k, text in ((5, "a += 5"), (1, "a++"), (-1, "a--"), (-32767, "a += 0 - 32767")):
            v, err = binary("+", a, k)
            yield f"addi {a} {text}", decl + text + "\nprint(a)\n", (str(v) if err is None else None, err), text
        for b in VALUES:
            decl2 = pre(a, b) + f"a := {build(a)}\nb := {build(b)}\n"
            for op in "+-*%":
                v, err = binary(op, a, b)
                yield f"{a} {op} {b}", decl2 + f"print(a {op} b)\n", (None if err else str(v), err), f"a {op} b"
                yield f"{a} {op}= {b}", decl2 + f"a {op}= b\nprint(a)\n", (None if err else str(v), err), f"a {op}= b"


def check(nift, work, case):
    name, source, (out, err), site_text = case
    d = tempfile.mkdtemp(dir=work)
    try:
        path = os.path.join(d, "p.f")
        with open(path, "w") as f:
            f.write(source)
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "HOME": d, "TMPDIR": d,
               "NIFT_VM": "require"}
        for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS", "NIFT_VM_MUTANT"):
            if k in os.environ:
                env[k] = os.environ[k]
        p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL, timeout=60)
        if err is None:
            want = ((out + "\n").encode(), b"", 0)
        else:
            src = source.encode()
            nodes = [n for n in vm_sites.parse(src).walk() if vm_sites.text(src, n) == site_text.encode()]
            line, column = vm_sites.position(src, min(n.begin for n in nodes))
            want = (b"", f"error: {path}:{line}:{column}: {err}\n".encode(), 1)
        got = (p.stdout, p.stderr, p.returncode)
        return None if got == want else f"{name}: got {got!r}, expected {want!r}"
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--jobs", type=int, default=2)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    work = tempfile.mkdtemp(prefix="nift-vm-int-bounds-")
    all_cases = list(cases())
    try:
        with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            failures = [f for f in pool.map(lambda c: check(nift, work, c), all_cases) if f]
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures[:20]:
        print("FAIL " + f)
    print(("PASS" if not failures else "FAIL") + f" vm int bounds: {len(all_cases)} programs (Python exact ints), "
          f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
