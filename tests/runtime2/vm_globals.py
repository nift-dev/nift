#!/usr/bin/env python3
"""CP12 phase B: globals (a function's free name resolved to a top-level
binding, rt/globals.h) on the VM against legacy, in every context.

  vm_globals.py --nift ./nift [--jobs 2] [--gen N --seed S] [--table]

Each shape (a global's declaration, the body of a function `gfun` that reads
or writes it, and an expression showing the global afterwards) runs in five
contexts: `gfun` called from the top level, from a loop, from a function
called from the top level, from a function called from a loop, and the
global used inside a loop of `gfun` itself. Legacy and NIFT_VM=1 must agree
exactly (or by a CP12 policy, vm_policies.py); each shape pins its outcome
per context (`vm`: on the VM, else the refusal reason before execution).
--gen adds random programs (globals of every kind, read and written by
several functions and by main, in every context): only agreement with
legacy is required, and the share run on the VM is reported. --table
prints the outcomes instead of checking the pins (to review a change).
"""
import argparse
import concurrent.futures as cf
import json
import os
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vm_policies  # noqa: E402

CONTEXTS = {
    "top": "{g}\nfn(gfun()) {{\n{b}\n}}\nprint(gfun())\nprint(gfun())\n{show}\n",
    "loop": "{g}\nfn(gfun()) {{\n{b}\n}}\ni0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n  print(gfun())\n}}\n{show}\n",
    "fn": "{g}\nfn(gfun()) {{\n{b}\n}}\nfn(wcall()) {{\n  print(gfun())\n  return 0\n}}\nprint(wcall())\nprint(wcall())\n{show}\n",
    "fnloop": "{g}\nfn(gfun()) {{\n{b}\n}}\nfn(wcall()) {{\n  print(gfun())\n  return 0\n}}\ni0 := 0\n"
              "while (i0 < 2) {{\n  i0 += 1\n  print(wcall())\n}}\n{show}\n",
    "inner": "{g}\nfn(gfun()) {{\n  j0 := 0\n  while (j0 < 2) {{\n    j0 += 1\n{b_inner}\n  }}\n  return 0\n}}\n"
             "print(gfun())\n{show}\n",
}

# (name, declaration, body (ends with a return), statements for `inner`
#  (no return), show, {context: expected} with "*" the default)
VM = {"*": "vm"}
SHAPES = [
    ("read-int", "g := 5", "return g + 1", "print(g * 2)", "print(g)", VM),
    ("read-string", 'g := "a"', 'return g + "b"', 'print(g + "c")', "print(g)", VM),
    ("read-double", "g := 1.5", "return g * 2", "print(g + 1)", "print(g)", VM),
    ("read-bool", "g := true", "return !g", "print(g)", "print(g)", VM),
    ("read-element", "g := [4, 5]", "return g[1]", "print(g[0])", "print(g.stringify())", VM),
    ("read-member", 'g := {"k": 3}', "return g.k", "print(g.k)", "print(g.stringify())", VM),
    ("read-length", 'g := "héllo"', "return g.length()", "print(g.length())", "print(g)", VM),
    ("assign", "g := 5", "g = g + 1\nreturn g", "g = g + 1", "print(g)", VM),
    # Loop code: legacy words the binding-type error differently (D-018c), as for a local.
    ("assign-type-error", "g := 5", 'g = "s"\nreturn 0', 'g = "s"', "print(g)",
     {"*": "vm", "loop": "context:assignment-error", "fnloop": "context:assignment-error",
      "inner": "context:assignment-error"}),
    ("assign-int-to-double", "g := 2.5", "g = 3\nreturn g", "g = 3", "print(g)", VM),
    # Loop-called code: a declaration typed from another binding's text, as for a local.
    ("assign-from-text-binding", "g := 2", "x := g\nx = x + 1\ng = x\nreturn g", "x := g\ng = x + 1",
     "print(g)", {"*": "vm", "loop": "prepared-decl-type", "fnloop": "prepared-decl-type"}),
    ("compound", "g := 5", "g += 2\ng -= 1\ng *= 3\nreturn g", "g += 2", "print(g)", VM),
    ("compound-self", "g := 3", "g += g\nreturn g", "g += g", "print(g)", VM),
    ("compound-double", "g := 1.5", "g += 1\nreturn g", "g += 1", "print(g)", VM),
    ("incdec", "g := 5", "g++\ng--\ng++\nreturn g", "g++", "print(g)", VM),
    ("string-append", 'g := "a"', 'g += "b"\nreturn g', 'g += "b"', "print(g)", VM),
    ("push", "g := [1]", "g.push(2)\nreturn g.length()", "g.push(3)", "print(g.stringify())", VM),
    ("push-own-length", "g := [1]", "g.push(g.length())\nreturn g.length()", "g.push(g.length())",
     "print(g.stringify())", VM),
    ("element-assign", "g := [1, 2]", "g[0] = g[1] + 5\nreturn g[0]", "g[1] = 9", "print(g.stringify())", VM),
    ("element-assign-range", "g := [1, 2]", "g[5] = 1\nreturn 0", "g[5] = 1", "print(g.stringify())", VM),
    ("main-writes-between", "g := 1", "return g * 10", "print(g)", "g = 7\nprint(gfun())", VM),
    # Refused: the binding a free name finds depends on the caller (L25, D-038).
    ("dynamic-caller", "q := 0", "return zz", "print(zz)", "print(q)",
     {"*": "free-name", "inner": "free-name"}),
]


def program(shape, ctx):
    name, decl, body, inner, show, _ = shape
    if name == "dynamic-caller":
        # zz exists only as a local of the calling function.
        src = CONTEXTS[ctx].format(g=decl, b=body, b_inner="    " + inner.replace("\n", "\n    "), show=show)
        return src.replace("fn(wcall()) {\n", "fn(wcall()) {\n  zz := 1\n")
    return CONTEXTS[ctx].format(g=decl, b="  " + body.replace("\n", "\n  "),
                                b_inner="    " + inner.replace("\n", "\n    "), show=show)


def run(nift, work, src, env):
    d = tempfile.mkdtemp(dir=work)
    with open(os.path.join(d, "t.f"), "w") as f:
        f.write(src)
    p = subprocess.run(["timeout", "30", nift, "t.f"], cwd=d, capture_output=True,
                       env=dict({"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d}, **env))
    stats = os.path.join(d, "stats.jsonl")
    st = json.loads(open(stats).read().splitlines()[-1]) if os.path.exists(stats) else {}
    return p.returncode, p.stdout, p.stderr.replace(d.encode(), b"$D"), st


def compare(nift, work, src):
    a = run(nift, work, src, {})
    b = run(nift, work, src, {"NIFT_VM": "1", "NIFT_VM_STATS": "stats.jsonl"})
    verdict, policy = vm_policies.classify((a[1], a[2], a[0]), (b[1], b[2], b[0]), b[3], src)
    outcome = b[3].get("outcome")
    label = "vm" if outcome in ("vm", "vm-error") else b[3].get("reason")
    problems = [] if verdict in ("same", "approved") else [f"legacy {a[:3]!r} != vm {b[:3]!r}"]
    return label, problems


def check_shape(args, work, item):
    shape, ctx = item
    src = program(shape, ctx)
    label, problems = compare(args.nift, work, src)
    want = shape[5].get(ctx, shape[5]["*"])
    if not args.table and label != want:
        problems.append(f"expected {want}, got {label}")
    return shape[0], ctx, label, problems, src


# ---- random programs --------------------------------------------------

KINDS = {"int": ["0", "3", "-2", "100"], "double": ["0.5", "2.25", "-1.5"], "string": ['"a"', '"xy"', '""'],
         "array": ["[1, 2]", "[0]"], "bool": ["true", "false"]}


S_LIT, T_LIT, BAD_LIT = '"s"', '"t"', '"bad"'


def random_program(seed, index):
    rng = random.Random(seed * 100003 + index)
    gs = {}
    for k in range(rng.randint(1, 3)):
        kind = rng.choice(list(KINDS))
        gs[f"g{k}"] = kind
    lines = [f"{n} := {rng.choice(KINDS[kind])}" for n, kind in gs.items()]

    def expr(kind):
        names = [n for n, k in gs.items() if k == kind]
        if kind == "int":
            atoms = names + ["1", "2"] + [f"{n}.length()" for n, k in gs.items() if k in ("array", "string")]
            return f"{rng.choice(atoms)} {rng.choice(['+', '-', '*'])} {rng.choice(atoms)}"
        if kind == "double":
            return f"{rng.choice(names + ['0.5'])} + {rng.choice(['1', '0.25'] + names)}"
        if kind == "string":
            return rng.choice(names + [S_LIT]) + " + " + rng.choice([T_LIT] + names)
        if kind == "bool":
            return f"!{rng.choice(names + ['true'])}"
        return rng.choice(names + ["[5]"])

    def stmt():
        n = rng.choice(list(gs))
        kind = gs[n]
        options = [f"{n} = {expr(kind)}"]
        if kind in ("int", "double"):
            options += [f"{n} += {rng.choice(['1', '2'])}", f"{n}++" if kind == "int" else f"{n} *= 2"]
        if kind == "string":
            options.append(f'{n} += "z"')
        if kind == "array":
            options += [f"{n}.push({rng.choice(['1', '7'])})", f"{n}[0] = {rng.choice(['3', '4'])}"]
        if rng.random() < 0.05:  # a type error
            options.append(n + " = " + rng.choice([BAD_LIT, "[1]", "2.5"]))
        return rng.choice(options)

    def show(n):
        return f"{n}.stringify()" if gs[n] == "array" else n

    fns = []
    for f in range(rng.randint(1, 3)):
        body = [stmt() for _ in range(rng.randint(1, 3))]
        if rng.random() < 0.3:
            body = ["j0 := 0", "while (j0 < 2) {", "  j0 += 1"] + ["  " + s for s in body] + ["}"]
        if f > 0 and rng.random() < 0.5:
            body.insert(0, f"f{f - 1}()")
        n = rng.choice(list(gs))
        body.append(f"return {show(n)}")
        fns.append(f"fn(f{f}()) {{\n  " + "\n  ".join(body) + "\n}")
    calls = []
    for _ in range(rng.randint(1, 4)):
        c = f"print(f{rng.randrange(len(fns))}())"
        r = rng.random()
        if r < 0.3:
            c = "i0 := 0\nwhile (i0 < 2) {\n  i0 += 1\n  " + c + "\n}"
        elif r < 0.45:
            c = stmt()
        calls.append(c)
    # one `i0` declaration at most at the top level
    seen, out = False, []
    for c in calls:
        if c.startswith("i0 := 0"):
            if seen:
                c = c.replace("i0 := 0\n", "i0 = 0\n", 1)
            seen = True
        out.append(c)
    prints = [f"print({show(n)})" for n in gs]
    return "\n".join(lines + fns + out + prints) + "\n"


def check_random(args, work, index):
    src = random_program(args.seed, index)
    label, problems = compare(args.nift, work, src)
    return index, label, problems, src


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--gen", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--table", action="store_true")
    args = ap.parse_args()
    args.nift = os.path.abspath(args.nift)
    items = [(s, c) for s in SHAPES for c in CONTEXTS]
    failures, tally = 0, {}
    with tempfile.TemporaryDirectory(prefix="vm-globals-") as work:
        with cf.ThreadPoolExecutor(args.jobs) as pool:
            for name, ctx, label, problems, src in pool.map(lambda it: check_shape(args, work, it), items):
                tally[label] = tally.get(label, 0) + 1
                if args.table:
                    print(f"{name:28} {ctx:7} {label}{'  DIFF' if problems else ''}")
                if problems:
                    failures += 1
                    print(f"FAIL {name} [{ctx}]: " + "; ".join(problems) + "\n" + src)
            print(f"shapes: {len(items)} programs ({len(SHAPES)} shapes x {len(CONTEXTS)} contexts); "
                  f"outcomes {dict(sorted(tally.items(), key=str))}")
            if args.gen:
                gen_tally = {}
                for index, label, problems, src in pool.map(lambda i: check_random(args, work, i), range(args.gen)):
                    gen_tally[label] = gen_tally.get(label, 0) + 1
                    if problems:
                        failures += 1
                        print(f"FAIL random seed {args.seed} #{index}: " + "; ".join(problems) + "\n" + src)
                on_vm = gen_tally.get("vm", 0)
                print(f"random: {args.gen} programs (seed {args.seed}); on the VM {on_vm} "
                      f"({100.0 * on_vm / args.gen:.1f} %); outcomes {dict(sorted(gen_tally.items(), key=str))}")
    status = "PASS" if failures == 0 else "FAIL"
    print(f"{status} vm globals: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
