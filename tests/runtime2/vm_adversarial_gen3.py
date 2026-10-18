#!/usr/bin/env python3
"""Runtime 2.0 CP11 review round 3: adversarial families for the VM parity
gate (registered in vm_adversarial_gen.FAMILIES). Deterministic; each yields
(family, name, source, env) or (family, name, source, env, expect), where
`expect` is an outcome-key prefix (or a tuple of prefixes) the VM statistics
must show; `fallback:<reason>` means "refused before execution with that
reason" (vm_adversarial.py checks it with NIFT_VM=classify, nothing runs).

  elemtarget  F1: element assignment `a[INDEX] = VALUE` with 33 index forms x
              11 value forms x 5 contexts (the reviewer's sweep shape). Only a
              plain local name or an unsigned integer literal is compiled as
              the index; every other index (anything whose text legacy may
              read as a member path: `a.length() - 1`, `o.k`, `1.0`,
              `g("x.y")`, and all computed forms) must be refused
              (`index-target`). Compound and `++`/`--` element targets are
              refused as well (`compound-target`, `incdec-target`).
  cmpchain    F2: every comparison whose operand is itself a comparison: the
              exhaustive two-operator sweep (6 x 6 operators, unparenthesised
              and parenthesised on either side) in eight contexts. All must
              be refused before execution (`comparison-chain`, or an Opaque
              class where the CP10 parser already refuses the shape). Single
              comparisons of every operator in the same contexts must compile.
  deeptree    F3: inputs whose tree depth or size would exhaust a recursive
              analysis: 100,000-term `+`, `-`, `&&`, `||`, `??`, `==` chains,
              100,000-deep parentheses, brackets, unary operators and calls,
              deep `if`/`while` nesting; each must fall back cleanly before
              execution (no crash). Long flat statement lists still compile.
"""

OPS = ["==", "!=", "<", "<=", ">", ">="]

ELEM_CTX = {
    "top": lambda b: b,
    "loop": lambda b: "i0 := 0\nwhile (i0 < 1) {\ni0 += 1\n" + b + "}\n",
    "if": lambda b: "if (true) {\n" + b + "}\n",
    "fn": lambda b: "fn(w0()) {\n" + b + "return 0\n}\nprint(w0())\n",
    "fnloop": lambda b: "fn(w0()) {\n" + b + "return 0\n}\ni0 := 0\nwhile (i0 < 1) {\ni0 += 1\nprint(w0())\n}\n",
}

# (index text, compiled?) -- 33 forms.
INDEXES = [
    ("i", True), ("j", True), ("0", True), ("1", True), ("2", True), (" i ", True), ("k", True), ("s", True),
    ("1.0", False), ("0.0", False), ("1e0", False), ("i + 0", False), ("i - 1", False), ("a.length() - 1", False),
    ("a.size() - 1", False), ("o.k", False), ('o["k"]', False), ('g("x.y")', False), ("g(0)", False),
    ("(i)", False), ("(1)", False), ("-1", False), ("-0", False), ("i * 1", False), ("[0][0]", False),
    ("a[0] - 1", False), ("(true ? 0 : 1)", False), ('"0"', False), ("null", False), ("true", False),
    ("0 + 1", False), ("2 % 3", False), ("d", True),
]
VALUES = ["9", "i", "-1", "2.5", '"s"', "a[0]", "o.k", "g(1)", "a.length()", "[1]", "i + 1"]


def elemtarget(_seed, _count):
    assert len(INDEXES) == 33 and len(VALUES) == 11
    pre = 'a := [1, 2, 3]\no := {"k": 1}\ni := 1\nj := 0\nk := 2\ns := "x"\nd := 1.5\n'
    post = "print(a[0])\nprint(a[1])\nprint(a[2])\n"
    for xi, (index, compiled) in enumerate(INDEXES):
        for vi, value in enumerate(VALUES):
            for cn, wrap in ELEM_CTX.items():
                body = pre + f"a[{index}] = {value}\n" + post
                src = "fn(g(x)) { return 0 }\n" + wrap(body)
                expect = None if compiled else ("fallback:index-target", "fallback:opaque:")
                yield "elemtarget", f"elemtarget-{xi}-{vi}-{cn}", src, {}, expect
    # Compound and increment element targets: refused whatever the index.
    for xi, (index, _) in enumerate(INDEXES[:8]):
        for cn, wrap in ELEM_CTX.items():
            for op in ("+= 1", "-= 1", "*= 2", "/= 2", "%= 2"):
                src = "fn(g(x)) { return 0 }\n" + wrap('a := [1, 2, 3]\ni := 1\nj := 0\nk := 2\ns := "x"\n'
                                                          f"d := 1.5\na[{index}] {op}\n" + post)
                yield "elemtarget", f"elemtarget-compound-{xi}-{op[0]}-{cn}", src, {}, "fallback:compound-target"
            for inc in (f"a[{index}]++", f"a[{index}]--", f"++a[{index}]", f"--a[{index}]"):
                src = "fn(g(x)) { return 0 }\n" + wrap('a := [1, 2, 3]\ni := 1\nj := 0\nk := 2\ns := "x"\n'
                                                          f"d := 1.5\n{inc}\n" + post)
                yield "elemtarget", f"elemtarget-incdec-{xi}-{inc[:2]}-{cn}", src, {}, (
                    "fallback:incdec-target", "fallback:opaque:")


CMP_CTX = {
    "print": lambda e: f"print({e})\n",
    "if": lambda e: f"if ({e}) {{ print(1) }} else {{ print(2) }}\n",
    "while": lambda e: f"c := 0\nwhile ({e} && c < 1) {{ c += 1 }}\nprint(c)\n",
    "ternary": lambda e: f"print(({e} ? 1 : 2))\n",
    "decl": lambda e: f"r := {e}\nprint(r)\n",
    "array": lambda e: f"r := [{e}]\nprint(r[0])\n",
    "fn-return": lambda e: f"fn(f()) {{ return {e} }}\nprint(f())\n",
    "fn-arg": lambda e: f"fn(f(p)) {{ return p }}\nprint(f({e}))\n",
}
OPERANDS = [("1", "1", "1"), ("1", "2", "true"), ('"a"', '"b"', "false"), ("x", "y", "z")]


def cmpchain(_seed, _count):
    pre = "x := 1\ny := 2\nz := true\n"
    refused = ("fallback:comparison-chain", "fallback:opaque:")
    for o1 in OPS:
        for o2 in OPS:
            for oi, (a, b, c) in enumerate(OPERANDS):
                for shape, e in (("flat", f"{a} {o1} {b} {o2} {c}"), ("left", f"({a} {o1} {b}) {o2} {c}"),
                                 ("right", f"{a} {o1} ({b} {o2} {c})")):
                    for cn, ctx in CMP_CTX.items():
                        if cn.startswith("fn") and a == "x":
                            continue  # functions refer only to their own names
                        body = ctx(e)
                        src = (body + pre) if cn.startswith("fn") else (pre + body)
                        if cn.startswith("fn"):  # function declarations come first
                            fn_end = body.index("}\n") + 2
                            src = body[:fn_end] + pre + body[fn_end:]
                        yield ("cmpchain", f"cmpchain-{OPS.index(o1)}{OPS.index(o2)}-{oi}-{shape}-{cn}", src, {},
                               refused)
    # Controls: a single comparison of every operator compiles in every context.
    for o1 in OPS:
        for oi, (a, b, _) in enumerate(OPERANDS):
            for cn, ctx in CMP_CTX.items():
                if cn.startswith("fn") and a == "x":
                    continue
                body = ctx(f"{a} {o1} {b}")
                if cn.startswith("fn"):
                    fn_end = body.index("}\n") + 2
                    src = body[:fn_end] + pre + body[fn_end:]
                else:
                    src = pre + body
                yield "cmpchain", f"cmpchain-single-{OPS.index(o1)}-{oi}-{cn}", src, {}, "vm"


def deeptree(_seed, _count):
    n = 100000
    deep = ("fallback:nesting-limit", "fallback:opaque:")
    shapes = {
        "plus": "print(" + " + ".join(["1"] * n) + ")\n",
        "minus": "print(" + " - ".join(["1"] * n) + ")\n",
        "and": "print(" + " && ".join(["true"] * n) + ")\n",
        "or": "print(" + " || ".join(["false"] * n) + ")\n",
        "coalesce": "print(" + " ?? ".join(["null"] * n) + " ?? 1)\n",
        "eq": "print(" + " == ".join(["1"] * n) + ")\n",
        "concat": "s := \"\"\ns = s" + ' + "a"' * n + "\nprint(s.length())\n",
        "parens": "print(" + "(" * n + "1" + ")" * n + ")\n",
        "brackets": "print(" + "[" * n + "1" + "]" * n + ")\n",
        "not": "print(" + "!" * n + "true)\n",
        "neg": "print(" + "-" * n + "1)\n",
        "calls": "fn(f(x)) { return x }\nprint(" + "f(" * n + "1" + ")" * n + ")\n",
        "index": "a := [0]\nprint(a" + "[0]" * n + ")\n",
        "if-nest": "if (true) {\n" * 2000 + "print(1)\n" + "}\n" * 2000,
        "while-nest": "".join(f"c{k} := 0\nwhile (c{k} < 1) {{\nc{k} += 1\n" for k in range(2000)) + "print(1)\n"
                      + "}\n" * 2000,
        "fn-plus": "fn(f()) { return " + " + ".join(["1"] * n) + " }\nprint(f())\n",
        "loop-plus": "i := 0\nwhile (i < 1) { i += 1\nprint(" + " + ".join(["1"] * n) + ") }\n",
        "fnloop-plus": ("fn(f()) { x := " + " + ".join(["1"] * n) + "\nreturn x }\ni := 0\n"
                        "while (i < 1) { i += 1\nprint(f()) }\n"),
    }
    for name, src in shapes.items():
        yield "deeptree", f"deeptree-{name}", src, {}, deep
    # Long flat statement lists and many functions: no depth, still compiled.
    yield "deeptree", "deeptree-statements", "x := 0\n" + "x += 1\n" * 50000 + "print(x)\n", {}, "vm"
    fns = "".join(f"fn(h{k}(x)) {{ return h{k + 1}(x) + 1 }}\n" for k in range(3000))
    yield "deeptree", "deeptree-function-chain", fns + "fn(h3000(x)) { return x }\nprint(h0(1))\n", {}, (
        "vm", "replayed:bail:recursion-budget", "fallback:")


FAMILIES = {"elemtarget": elemtarget, "cmpchain": cmpchain, "deeptree": deeptree}
