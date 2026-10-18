#!/usr/bin/env python3
"""Runtime 2.0 CP12: the review round 1 reproducers, permanent.

  vm_review_cp12.py --nift ./nift [--timeout S]

Ports the reviewer's repro scripts (nc2.py, nc3.py, edge.py, edge2.py,
edge3.py, big.py; mask.py and strict.py are vm_policy_negative.py). Every
program runs on legacy and with NIFT_VM=1; each must be identical, refused
before execution (fallback, with the expected reason where the finding
names one), or a policy-approved D-018 difference with its per-line
evidence (vm_policies.classify). Never a crash, an internal failure (exit
70) or an unapproved difference.

  D1  kind inference gave up after 200 passes on call chains and switched
      the aliasing/type/context refusals off: chains of 199/201 functions
      (never called) followed by programs that must be refused.
  D2  INT64_MIN % -1 (SIGFPE) as `%`, `%=`, through a call, in a loop.
  D3  -INT64_MIN (an exact Decimal) printed, concatenated, used.
  D4  large operands in loop code (big - big2, m == m + 0, ...): evidence
      per line, approved only where attributed.
"""
import argparse
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vm_policies  # noqa: E402
import vm_sites  # noqa: E402
from vm_adversarial import run  # noqa: E402


def chain(n):
    parts = ["fn(g0(a)) { return a }\n"]
    for k in range(1, n):
        parts.append(f"fn(g{k}(a)) {{\n  x := a\n  return g{k - 1}(x)\n}}\n")
    return "".join(parts)


def programs():
    """(group, name, source, expectation): expectation is None (any accepted
    verdict) or a fallback reason the VM must report."""
    out = []
    # D1 (nc2.py, nc3.py)
    for n in (199, 201):
        guard = f"if (0 > 1) {{ print(g{n - 1}(1)) }}\n"
        for name, tail, reason in (
                ("alias", "a := [1]\nb := a\nb.push(2)\nprint(a.length())\n", "aliasing"),
                ("negzero", "x := 0\ni := 0\nwhile (i < 1) { i += 1\nprint(-x) }\n", "context:negative-zero"),
                ("order", 'fn_v := 1\nprint("x" < 1)\n', "type:<"),
                ("assign", "x := 1\ni := 0\nwhile (i < 2) { x = x + 0.5; i++ }\nprint(x)\n", "context:assignment-type"),
                ("objlen", None, "context:object-length"),
                ("aliasret", None, "aliasing"),
                ("strtype", 'x := 1\nprint(x + "s")\n', None)):
            if name == "objlen":
                src = 'fn(nn(o)) { return o.length() }\n' + chain(n) + guard + \
                    'i := 0\nwhile (i < 1) { print(nn({"k": 1})); i++ }\n'
            elif name == "aliasret":
                src = 'fn(f(a)) { return a }\n' + chain(n) + guard + 'q := [1]\nr := f(q)\nr.push(5)\nprint(q.length())\n'
            else:
                src = chain(n) + guard + tail
            out.append(("D1", f"chain{n}-{name}", src, reason))
    # D2 (edge2.py) and D3 (edge3.py)
    pre = "q := 2147483648\nh := q * q\nm := (0 - h) * 2\nn := 0 - 1\n"
    for e in ["-m", "m % 1", "m % m", "m % (0 - 1)", "m % -1", "m / -1", "m - 0", "m * 1", "m % -2", "(m + 1) % -1"]:
        out.append(("D2", f"top {e}", pre + f"print({e})\n", None))
    out.append(("D2", "modassign", pre + "m %= n\nprint(m)\n", None))
    out.append(("D2", "call", "fn(f(a, b)) { return a % b }\nq := 2147483648\nprint(f((0 - q * q) * 2, 0 - 1))\n", None))
    out.append(("D2", "loop", pre + "i := 0\nwhile (i < 2) { i += 1\nprint(m % n) }\n", None))
    dpre = "q := 2147483648\nh := q * q\nm := (0 - h) * 2\nd := -m\n"
    for e in ["d", "d + 1", "d - 1", "d * 1", "d / 2", "d % 3", "d == d", "d > 1", "d.length()", "[d]", 'd + "s"',
              '"s" + d']:
        out.append(("D3", f"use {e}", dpre + f"x := {e}\nprint(\"ok\")\n", None))
    for e in ["d", "-d", 'd + "s"', '"s" + d', "a[d]"]:
        for ctx in ("top", "loop"):
            body = f"print({e})\n"
            src = dpre + "a := [1]\n" + (body if ctx == "top" else f"i := 0\nwhile (i < 1) {{ i += 1\n{body}}}\n")
            out.append(("D3", f"{ctx} print {e}", src, None))
    # D4 (edge.py, big.py)
    for e in ["m", "m / n", "m % n", "m * n", "0 - m", "m - 1", "m + m", "m / 2", "m % 3", "h * 2 - 1",
              "(h - 1) * 2 + 1", "m / 0.5", "m == m + 0", "h / 3", "m % 2.5"]:
        out.append(("D4", f"top {e}", pre + f"print({e})\n", None))
        out.append(("D4", f"loop {e}", pre + f"i := 0\nwhile (i < 1) {{ i += 1\nprint({e}) }}\n", None))
    w = "w := 9007199254740991\nbig := w + 2\nbig2 := w + 1\n"
    for body in ["print(big)", 'if (big == big2) { print("eq") } else { print("ne") }', "print(big > big2)",
                 "t := 0\nt = big\nprint(t)", "print(big - big2)", "print([big][0])", "print(big2 < big)",
                 "a := [big]\nprint(a[0] - 1)"]:
        out.append(("D4", f"big {body.splitlines()[-1]}", w + "i := 0\nwhile (i < 1) { i += 1\n" + body + " }\n", None))
    return out


# D4 programs where legacy (loop code, doubles) and the VM (CP09 exact
# integers) differ by D-018a: the VM's result is pinned, computed here with
# Python ints (review round 2: no policy approves numbers). An error is
# (expression text whose node fails, message); its position comes from the
# CP10 AST (vm_sites), never from the VM.
M, H = -(1 << 63), 1 << 62
WW = 9007199254740991
OVF = "signed 64-bit integer overflow"
EXACT = {
    "loop m * n": ("", ("m * n", OVF)),        # (-2^63) * -1 = 2^63
    "loop 0 - m": ("", ("0 - m", OVF)),        # 2^63
    "loop m - 1": ("", ("m - 1", OVF)),        # -2^63 - 1
    "loop m + m": ("", ("m + m", OVF)),        # -2^64
    "loop h * 2 - 1": ("", ("h * 2", OVF)),    # h * 2 = 2^63 fails before the subtraction
    "loop (h - 1) * 2 + 1": (f"{(H - 1) * 2 + 1}\n", None),
    "loop m == m + 0": (f"{str(M == M + 0).lower()}\n", None),
    "big print(big - big2)": (f"{(WW + 2) - (WW + 1)}\n", None),
    "big print(a[0] - 1)": (f"{(WW + 2) - 1}\n", None),
}


def exact_result(name, source, path):
    out, error = EXACT[name]
    if error is None:
        return out.encode(), b"", 0
    line, column = vm_sites.position(source.encode(), min(
        n.begin for n in vm_sites.parse(source.encode()).walk()
        if vm_sites.text(source.encode(), n) == error[0].encode()))
    return out.encode(), f"error: {path}:{line}:{column}: {error[1]}\n".encode(), 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--timeout", type=float, default=60)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    work = tempfile.mkdtemp(prefix="nift-vm-review-cp12-")
    failures, tally = [], {}
    try:
        for group, name, source, reason in programs():
            d = tempfile.mkdtemp(dir=work)
            path = os.path.join(d, "p.f")
            with open(path, "w") as f:
                f.write(source)
            legacy, _ = run(nift, path, d, None, {}, args.timeout)
            vm, st = run(nift, path, d, "1", {}, args.timeout)
            verdict, policy = vm_policies.classify(legacy, vm, st, source)
            outcome = st and st["outcome"]
            if name in EXACT and group == "D4":
                pinned = exact_result(name, source, path)
                verdict, policy = ("pinned", "exact") if vm == pinned else ("diff", None)
            key = f"{group} {outcome}" + (f" {policy}" if policy else "")
            tally[key] = tally.get(key, 0) + 1
            if reason is not None and (outcome != "fallback" or st.get("reason") != reason):
                failures.append(f"{group} {name}: expected refusal {reason}, got {outcome} {st and st.get('reason')}")
            elif not isinstance(vm[2], int) or vm[2] < 0 or vm[2] == 70 or outcome in ("internal-error", None):
                failures.append(f"{group} {name}: crash or internal failure (exit {vm[2]}, {outcome})")
            elif verdict not in ("same", "approved", "pinned"):
                failures.append(f"{group} {name}: {verdict} ({outcome} {st and st.get('reason')}): legacy {legacy!r} "
                                f"vm {vm!r}")
            shutil.rmtree(d, ignore_errors=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures:
        print("FAIL " + f)
    total = sum(tally.values())
    print(f"vm review CP12 round 1 reproducers: {dict(sorted(tally.items()))}")
    print(("PASS" if not failures else "FAIL") + f" vm review reproducers: {total} programs, {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
