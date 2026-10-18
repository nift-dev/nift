#!/usr/bin/env python3
"""Runtime 2.0 CP12: the former-replay adversarial suite (D-013 removal gate).

  vm_former_replay.py --nift ./nift [--seed S] [--sample N] [--families a,b] [--jobs J]

CP11 replayed a unit on legacy when the VM reached a case it did not
reproduce (runtime-error, unverified, number-range, aliasing,
recursion-budget, output-limit, nesting-depth). CP12 removed replay. This
suite aims programs at exactly those cases, in every context legacy
distinguishes (top level, `if` blocks, loop bodies, functions called from
top level, from loops, from both, nested calls, a function's own loop):

  errors       value-dependent run-time errors (division and modulo by zero,
               index out of range / negative / fractional, missing key or
               member, integer overflow, length() on an object) inside every
               expression shape and statement kind
  types        binding type changes (int to double, string to number, ...),
               compound assignments and increments, element type changes
  numbers      numeric boundaries: 2^53 +- k, int64 overflow, -0, large
               doubles, values legacy re-evaluates in a loop statement
               (including a call with output in that statement; D-018a)
  recursion    depths around legacy's 64 frames and the VM's budget, memory
  aggregates   declarations, returns, pushes, literals and element
               assignments from places holding scalars or aggregates
  methods      length() / size() / push on every receiver kind
  calls        functions invoking functions, errors deep in call chains,
               "return: " prefixes, one function called from both legacy
               evaluators (CP12 D-018: one VM meaning)
  loops        loop bodies with statements legacy's prepared parser rejects
               (ternaries), print(path), calls with rejected arguments
  output       megabytes of output followed by an end, an error, a large
               number or the recursion limit

Every program must either run on the VM (outcome vm or vm-error) with
stdout, stderr and exit status identical to legacy's (or a difference
approved by a named policy of vm_policies.py: D-001#4 recursion, D-018a
numbers, D-018c diagnostics), or be refused before execution (outcome
fallback, vm_executions 0, execute_us 0) with legacy's output. A `replayed`
outcome, a unit executed other than exactly once, an internal error or any
other difference fails. The outcome shares are reported.
"""
import argparse
import collections
import concurrent.futures as cf
import os
import random
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vm_adversarial import classify_difference, outcome_key, run  # noqa: E402

PRE = "fn(id(q)) { return q }\nfn(id2(p, q)) { return q }\n"
SETUP = ('z := 0\nh := 5.5\narr := [1, 2]\no := {"k": 1, "s": "t"}\nbig := 2000000000 * 2000000000\n'
         'k5 := 5\nneg := -1\nfr := 0.5\ntwo := 2.0\nn := 7\ng := 2.5\nw := 9007199254740991\nv2 := 0\nks := "zz"\n')

ERRORS = {"div0": "1 / z", "mod0": "1 % z", "modfloat": "h % 2", "idx": "arr[k5]", "idxneg": "arr[neg]",
          "idxfr": "arr[fr]", "idx2": "arr[two]", "key": 'o["z"]', "keyv": "o[ks]", "mem": "o.z",
          "ovf": "big * 3", "ovf2": "big + big + big", "sub": "neg - big - big - big", "lenobj": "o.length()",
          "sizeobj": "o.size()", "ok": "n * 2", "large": "w + 2", "largem": "w * w", "neg0": "z * neg",
          "negz": "-z", "divf": "n / 2"}
WRAPS = ["{E}", "2 + {E}", "{E} + 2", "({E})", "2 + ({E})", "(2 + {E})", "2 * {E}", "-({E})", "[{E}]", "{E} == 1",
         "!({E})", "{E} && 1", "1 && {E}", "(1 < 2 ? {E} : 0)", "null ?? {E}", "id({E})", "id2(1, {E})",
         "2 + 3 * {E}", "(1 + 1) + {E}", "arr[{E}]", '"s" + {E}']
STATEMENTS = {"print": "print({X})", "decl": "v1 := {X}", "assign": "v2 = {X}", "if": "if ({X}) {{ print(0) }}",
              "elif": "if (false) {{ print(9) }} else if ({X}) {{ print(0) }}", "ret": "return {X}",
              "push": "arr.push({X})", "while": "while ({X}) {{ print(0)\nbreak }}", "compound": "v2 += {X}"}

TYPES = ["n /= 2", "n = n / 2", "n = h", "n -= h", "n = 0.5", 'h = "s"', "g = n", "n += h", "n++\nn = 0.5",
         'arr[0] = "s"\nprint(arr[0])', "arr[1] = 2.5\nprint(arr[1])", 's := "a"\ns += "b"\nprint(s)',
         "u := 3\nu = u * 0.5\nprint(u)", "u := 1.5\nu = 2\nprint(u)\nu = u + 1\nprint(u)", 'b0 := null\nb0 = 1',
         "b1 := true\nb1 = 1", 'b2 := "x"\nb2 = "y"\nprint(b2)', "b3 := 2.0\nb3 = 2.5\nprint(b3)",
         "arr[k5] = 1", "arr[neg] = 1", "arr[fr] = 1", "arr[two] = 3\nprint(arr)"]
NUMBERS = ["big *= 3", "big += big", "big++\nprint(big)", "w++\nprint(w)\nw++\nprint(w)", "q := w + 2\nprint(q)",
           "q := w * 3\nprint(q)", "q := 1\nq = w * 4\nprint(q)", "q := w\nq += 5\nprint(q)", "print(w * 3)",
           "print(w + 1 - 1)", 'x := z * neg\nprint(x)\nprint("a" + x)', "print(1 / (z * neg))", "print(big)",
           "print(big + 1)", "print(big * 2)", "print(-big)", "c := big / 3\nprint(c)", "d := w + 0.5\nprint(d)",
           "print(-7 % 3)\nprint(7 / 2)\nprint(-4 % 2)", "m := 4611686018427387904\n",
           "e := 1000000000000000\ne = e * e * e * e * e * e * e * e * e * e * e * e * e * e * e * e * e * e * e * e"
           " * e * e\nprint(e)", "t := loud(w) + 2\nprint(t)", "t := 0\nt = loud(w) * 2\nprint(t)",
           "t := 1\nt += loud(w)\nprint(t)", "print(loud(w) + 2)"]
AGGREGATES = ["a2 := arr\nprint(a2.length())", "a2 := [arr]\nprint(1)", "a2 := arr[0]\nprint(a2)",
              "a2 := o.k\nprint(a2)", 'a2 := o["s"]\nprint(a2)', "p := [[1, 2], [3]]\nq := p[0]\nprint(q.length())",
              "p := [[1, 2], [3]]\nprint(p[1][0])", 'p := {"a": [1], "b": {"c": 2}}\nprint(p.b.c)',
              'p := [1, {"z": 4}]\nprint(p[1].z)', 'p := [1, {"z": 4}]\ni := 1\nprint(p[i].z)',
              "arr.push(arr)", "arr.push([1])", "arr.push(3)\nprint(arr.length())", "arr[0] = [1]",
              "r := mk()\nprint(r.length())", "r := mk()\nr.push(1)\nprint(r.length())", "print(id(arr) == arr)",
              "print(arr == [1, 2])", "s := [arr[0], arr[1]]\nprint(s[1])"]
METHODS = [f"print({r}.{m}())" for r in ("arr", "o", '"héllo"', "n", "h", "null", "true", "ks") for m in ("length", "size")]
METHODS += ["arr.push(1, 2)", "n.push(1)", "o.push(1)", "ks.push(1)", "print(arr.length() + o.size())"]
CALLS = ["print(c1(3))", "print(c1(0))", "r := c1(2)\nprint(r)", "print(c2(z))", "print(c3(arr, k5))",
         "print(c3(arr, 1))", "print(c4(n))", "print(c5(0))", "print(c5(3))", "r := c4(h)\nprint(r)",
         "print(c6(1))", "print(c6(70))", "v2 = c1(5)\nprint(v2)", "print(c7(o))", "print(c7(arr))"]
CALL_FNS = ("fn(loud(x)) { print(\"loud\")\nreturn x }\n"
            "fn(mk()) { return [1, 2, 3] }\n"
            "fn(c1(x)) { if (x == 0) { return 1 / x }\nreturn c1(x - 1) + 1 }\n"
            "fn(c2(x)) { return id(10 / x) }\n"
            "fn(c3(a, i)) { return a[i] }\n"
            "fn(c4(x)) { y := x\ny = y + 0.5\nreturn y }\n"
            "fn(c5(x)) { k := 0\nwhile (k < x) { k += 1 }\nreturn 10 / k }\n"
            "fn(c6(d)) { if (d == 0) { return 0 }\nreturn c6(d - 1) + 1 }\n"
            "fn(c7(v)) { return v.length() }\n")
LOOPS = ["x := (n > 1 ? 1 / z : 0)", "print((n > 1 ? arr[k5] : 0))", "print(arr[k5])", "print(arr[two])",
         "print(arr[neg])", "print(o.z)", 'print(o["z"])', "print(arr[n - 6])", "print(arr[n + 1])",
         "print(id2((n > 1 ? 1 : 0), 1 / z))", "x := id((true ? w : 0) + 2)\nprint(x)", "print(arr[o.length()])",
         "print(id((true ? o.length() : 0)))", "y := (true ? w : 0) + 2\nprint(y)"]
RECURSION = ["fn(d(n)) {{ if (n == 0) {{ return 0 }}\nreturn d(n - 1) + 1 }}\nprint(\"s\")\nprint(d({n}))\n",
             "fn(d(n)) {{ if (n == 0) {{ return 1 / n }}\nreturn d(n - 1) }}\nprint(\"s\")\nprint(d({n}))\n",
             "fn(d(n, s)) {{ t := s + \"xxxxxxxx\"\nif (n == 0) {{ return 0 }}\nreturn d(n - 1, t) + 1 }}\n"
             "print(\"s\")\nprint(d({n}, \"a\"))\n",
             "fn(d(n)) {{ if (n == 0) {{ return 0 }}\nreturn d(n - 1) + 1 }}\ni := 0\n"
             "while (i < 1) {{ i += 1\nprint(d({n})) }}\n"]


def contexts(st, fns=""):
    pre = PRE + fns
    yield "top", pre + SETUP + 'print("before")\n' + st + '\nprint("after")\n'
    yield "topif", pre + SETUP + 'print("before")\nif (true) {\n' + st + '\n}\nprint("after")\n'
    yield "loop", pre + SETUP + 'print("before")\ni0 := 0\nwhile (i0 < 1) {\ni0 += 1\n' + st + '\n}\nprint("after")\n'
    fn = "fn(w0()) {\n" + SETUP + st + "\nreturn 7\n}\n"
    yield "fntop", pre + fn + 'print("before")\nprint(w0())\nprint("after")\n'
    yield "fntopdecl", pre + fn + 'print("before")\nr := w0()\nprint(r)\n'
    yield "fnloop", pre + fn + 'print("before")\ni0 := 0\nwhile (i0 < 1) { i0 += 1\nprint(w0()) }\nprint("after")\n'
    yield "fnown", (pre + "fn(w0()) {\n" + SETUP + "j0 := 0\nwhile (j0 < 1) {\nj0 += 1\n" + st + "\n}\nreturn 7\n}\n"
                    + 'print("before")\nprint(w0())\n')
    yield "fnnest", pre + fn + "fn(w1()) {\nreturn w0()\n}\n" + 'print("before")\nprint(w1())\n'
    yield "fnnestloop", (pre + fn + "fn(w1()) {\nreturn w0()\n}\n"
                         + 'print("before")\ni0 := 0\nwhile (i0 < 1) { i0 += 1\nprint(w1()) }\n')
    yield "fnboth", (pre + fn + 'print("before")\nprint(w0())\ni0 := 0\n'
                     + "while (i0 < 1) { i0 += 1\nprint(w0()) }\n")


def programs(seed):
    for ek, e in ERRORS.items():
        for wrap in WRAPS:
            for sk, s in STATEMENTS.items():
                st = s.format(X=wrap.format(E=e))
                for c, src in contexts(st):
                    if sk == "ret" and c in ("topif", "loop"):
                        continue
                    yield "errors", f"errors-{ek}-{sk}-{c}-{WRAPS.index(wrap)}", src
    for family, items, fns in (("types", TYPES, ""), ("numbers", NUMBERS, CALL_FNS), ("aggregates", AGGREGATES, CALL_FNS),
                               ("methods", METHODS, ""), ("calls", CALLS, CALL_FNS), ("loops", LOOPS, "")):
        for k, st in enumerate(items):
            for c, src in contexts(st, fns):
                yield family, f"{family}-{k}-{c}", src
    for k, template in enumerate(RECURSION):
        for n in (63, 64, 65, 1000, 99990, 100005):
            yield "recursion", f"recursion-{k}-{n}", template.format(n=n)
    loop = 'p := "{line}"\ni0 := 0\nwhile (i0 < {n}) {{ print(p + i0); i0 += 1 }}\n'
    rec = "fn(d(n)) { if (n == 0) { return 0 }\nreturn d(n - 1) }\n"
    for n in (60000, 200000):
        for tail, label in (("", "end"), ("print(1 / 0)\n", "error"), ("x9 := 9007199254740991\nprint(x9 + 2)\n", "large"),
                            ("print(d(70))\n", "recursion"), ("print(d(100005))\n", "budget"), ('return "done"\n', "return")):
            yield "output", f"output-{n}-{label}", rec + loop.format(line="x" * 20, n=n) + tail


def check(nift, item, work, timeout):
    family, name, source = item
    d = tempfile.mkdtemp(dir=work)
    path = os.path.join(d, "p.f")
    with open(path, "w") as f:
        f.write(source)
    try:
        legacy, _ = run(nift, path, d, None, {}, timeout)
        vm, st = run(nift, path, d, "1", {}, timeout)
        key = outcome_key(st) if vm[2] != "timeout" else "timeout"
        verdict = classify_difference(legacy, vm, st)
        problem = None
        if not st:
            problem = "no statistics"
        elif st["outcome"] in ("replayed", "internal-error"):
            problem = f"outcome {st['outcome']}"
        elif st.get("vm_executions", 0) + st.get("legacy_executions", 0) != 1:
            problem = f"executions vm={st.get('vm_executions')} legacy={st.get('legacy_executions')}"
        elif st["outcome"] == "fallback" and (st.get("vm_executions") or st.get("execute_us", 0) != 0):
            problem = "fallback after VM execution started"
        elif st["outcome"] not in ("vm", "vm-error", "fallback"):
            problem = f"unexpected outcome {st['outcome']}"
        elif verdict not in ("same", "endless") and not verdict.startswith("approved:"):
            problem = "output differs from legacy"
        return family, name, key, verdict, problem, source, legacy, vm
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--sample", type=int, default=0, help="run a seeded sample of at most N programs per family")
    ap.add_argument("--families", default="")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--timeout", type=float, default=30)
    ap.add_argument("--keep", help="directory to keep failing programs in")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    by_family = collections.defaultdict(list)
    for item in programs(args.seed):
        by_family[item[0]].append(item)
    wanted = [f for f in args.families.split(",") if f] or list(by_family)
    rng = random.Random(args.seed)
    items = []
    for family in wanted:
        progs = by_family[family]
        if args.sample and len(progs) > args.sample:
            progs = rng.sample(progs, args.sample)
        items += progs
    work = tempfile.mkdtemp(prefix="nift-vm-former-")
    tally = collections.defaultdict(collections.Counter)
    failures = []
    try:
        with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for family, name, key, verdict, problem, source, legacy, vm in pool.map(
                    lambda it: check(nift, it, work, args.timeout), items):
                tally[family][key.split(":", 1)[0] if key.startswith("fallback") else key] += 1
                if verdict.startswith("approved:"):
                    tally[family][verdict] += 1
                if problem:
                    failures.append((name, key, problem, source, legacy, vm))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if args.keep and failures:
        os.makedirs(args.keep, exist_ok=True)
        for name, _key, _problem, source, _legacy, _vm in failures:
            with open(os.path.join(args.keep, name + ".f"), "w") as f:
                f.write(source)
    for name, key, problem, source, legacy, vm in failures[:25]:
        print(f"FAIL {name} [{key}]: {problem}")
        print("  program:\n" + "\n".join("    " + l for l in source.splitlines()[:50]))
        print(f"  legacy: exit={legacy[2]} out={legacy[0][-200:]!r} err={legacy[1][:300]!r}")
        print(f"  vm:     exit={vm[2]} out={vm[0][-200:]!r} err={vm[1][:300]!r}")
    total = collections.Counter()
    for family in wanted:
        total.update(tally[family])
        print(f"{family}: {dict(tally[family])}")
    n = len(items)
    on_vm = total["vm"] + total["vm-error"]
    print(f"former replay suite (seed {args.seed}): {n} programs; on the VM {on_vm} "
          f"({100.0 * on_vm / max(1, n):.1f} %: vm {total['vm']}, vm-error {total['vm-error']}), "
          f"refused before execution {total['fallback']}, replayed 0 required (seen {total['replayed']}), "
          f"approved {dict(sorted((k[9:], n) for k, n in total.items() if k.startswith('approved:')))}")
    print(("PASS" if not failures else "FAIL") + f" vm former replay: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
