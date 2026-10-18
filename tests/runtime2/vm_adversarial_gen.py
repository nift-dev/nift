#!/usr/bin/env python3
"""Runtime 2.0 CP11: adversarial program generators for the VM parity gate
(tests/runtime2/vm_adversarial.py). Every generator is deterministic for a
seed and yields (family, name, source, env) tuples. They aim at feature
INTERACTIONS where a compiled program could silently differ from legacy:

  typed        typed random programs (ported from the CP11 review's fuzz2/
               fuzz3): int/double mixes, zero and negative-zero decimal
               literals, strings, bools, arrays/objects, call DAGs, guarded
               recursion, nested loops, `break`/`continue` both last and
               followed by statements in their block.
  context      the same straight-line snippet at top level, in a loop body,
               in a function called from top level and from a loop body.
  numeric      literal spellings (zeros, exponents, digit counts, leading
               zeros) through print, negation, concatenation, binding,
               aggregates, returns, arithmetic, in every context.
  receiver     every supported method / index / member / operator on every
               receiver kind (receiver-specific method availability).
  names        every identifier-shaped string literal of the legacy evaluator
               source plus a word list, as function, parameter, binding and
               member names (reserved names, built-in shadowing).
  alias        place arguments, declarations, returns, pushes and literals
               against mutating and read-only callees (mutation/aliasing).
  control      jumps and returns at every position of nested blocks.
  recursion    depths around the legacy limit (64) and the VM budget, with
               growing strings (deep recursion, memory budget).
  output       large output followed by a normal end, a run-time error, a
               large number, the recursion limit or the recursion budget
               (CP12: the VM streams its output; NIFT_VM_FLUSH lowers the
               streaming threshold), and one run of ~70 MiB ending in an error.
  malformed    near-miss mutants of typed programs (vm_gen.mutate).
  typing       binding types from declaration text referring to other
               bindings, then every kind of assignment, in every context.
"""
import glob
import os
import random
import re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# Review round 2: magnitudes keep generated values within +-2^53 (no D-018a
# difference can arise; numbers beyond it are pinned in vm_golden.py).
INTS = ["0", "1", "2", "3", "5", "10", "7", "100", "12", "1000", "255", "32768", "400"]
NINTS = ["-1", "-2", "-7", "-32769"]
DBLS = ["0.0", "0.5", "1.5", "2.0", "0.1", "0.2", "1e3", "3.25", "1.0", "1e-3", "0.3", "2.5e2", "100.25", "1e2",
        "0.125", "0.00", "0.0e0"]
NDBLS = ["-0.5", "-2.5", "-0.0", "-1.0"]
STRS = ['"a"', '"b"', '""', '"0"', '"1.5"', '"é"', '"ab"', '"A"', '"null"', "'z'", '"false"', '" "', '"✓x"']


class Typed:
    """Port of the CP11 review's typed fuzzers. mid_jumps: a jump may be
    followed by more statements in its block (fuzz2); otherwise it ends the
    block (fuzz3). Loop counters (kind "c") are read like numbers but never
    assigned, so generated loops terminate."""

    def __init__(self, rng, mid_jumps, clean=False):
        self.r, self.n, self.fns, self.mid = rng, 0, [], mid_jumps
        # clean: no zero-valued decimal spellings and no `?:` inside if/while
        # conditions (both refused statically), so more programs run on the VM.
        self.clean, self.in_cond = clean, False
        zero = {"0.0", "0.00", "0.0e0", "-0.0"}
        self.dbls = [x for x in DBLS if not (clean and x in zero)]
        self.ndbls = [x for x in NDBLS if not (clean and x in zero)]

    def condition(self, sc):
        self.in_cond = True
        try:
            return self.cond(sc, 1)
        finally:
            self.in_cond = False

    def ternary_ok(self):
        return not (self.clean and self.in_cond)

    def tcond(self, sc, d):
        """Ternary condition; the clean variant avoids a leading `!` (legacy
        reads `!x ? a : b` irregularly; refused statically)."""
        c = self.cond(sc, d)
        for _ in range(5):
            if not (self.clean and c.startswith("!")):
                break
            c = self.cond(sc, d)
        return c

    def ch(self, *a):
        return self.r.choice(a)

    def v(self, sc, k):
        c = [n for n, kk in sc if kk == k or (k == "n" and kk == "c")]
        return self.r.choice(c) if c else None

    def target(self, sc, k):
        c = [n for n, kk in sc if kk == k]
        return self.r.choice(c) if c else None

    def num(self, sc, d=0):
        r = self.r.random()
        if d > 2 or r < .3:
            x = self.v(sc, "n")
            if x and self.r.random() < .65:
                return x
            return self.ch(*(INTS + self.dbls + ["(" + self.ch(*(NINTS + self.ndbls)) + ")"]))
        if r < .6:
            return f"{self.num(sc, d + 1)} {self.ch('+', '-', '*', '/', '%', '+', '*', '-')} {self.num(sc, d + 1)}"
        if r < .67:
            return f"({self.num(sc, d + 1)})"
        if r < .72:
            x = self.v(sc, "n")
            return f"-{x}" if x else f"-({self.num(sc, d + 1)})"
        if r < .80 and self.fns:
            n, a = self.r.choice(self.fns)
            return f"{n}({', '.join(self.num(sc, d + 2) for _ in range(a))})"
        if r < .86:
            a = self.v(sc, "a")
            if a:
                return self.ch(f"{a}.length()", f"{a}.size()", f"{a}[0]", f"{a}[{a}.length() - 1]")
        if r < .90:
            o = self.v(sc, "o")
            if o:
                return self.ch(f"{o}.k", f'{o}["k"]', f"{o}.size()")
        if r < .94:
            t = self.v(sc, "s")
            if t:
                return self.ch(f"{t}.length()", f"{t}.size()")
        if r < .97 and self.ternary_ok():
            return f"({self.tcond(sc, d + 1)} ? {self.num(sc, d + 1)} : {self.num(sc, d + 1)})"
        if not self.ternary_ok():
            return f"({self.num(sc, d + 1)})"
        return f"({self.ch('null', self.v(sc, 'n') or '1')} ?? {self.num(sc, d + 1)})"

    def st(self, sc, d=0):
        r = self.r.random()
        if d > 2 or r < .4:
            x = self.v(sc, "s")
            if x and self.r.random() < .6:
                return x
            return self.ch(*STRS)
        if r < .85 or not self.ternary_ok():
            return f"{self.st(sc, d + 1)} + {self.ch(self.st(sc, d + 1), self.num(sc, d + 2), self.cond(sc, 3))}"
        return f"({self.tcond(sc, d + 1)} ? {self.st(sc, d + 1)} : {self.st(sc, d + 1)})"

    def cond(self, sc, d=0):
        r = self.r.random()
        if d > 2 or r < .15:
            x = self.v(sc, "b")
            return x if x and self.r.random() < .5 else self.ch("true", "false")
        if r < .55:
            return f"{self.num(sc, d + 1)} {self.ch('<', '<=', '>', '>=', '==', '!=')} {self.num(sc, d + 1)}"
        if r < .65:
            return f"{self.st(sc, d + 1)} {self.ch('<', '==', '!=', '>=')} {self.st(sc, d + 1)}"
        if r < .80:
            return f"({self.cond(sc, d + 1)}) {self.ch('&&', '||')} ({self.cond(sc, d + 1)})"
        if r < .86:
            return f"!({self.cond(sc, d + 1)})"
        if r < .94:
            return f"{self.num(sc, d + 1)} {self.ch('&&', '||')} {self.st(sc, d + 1)}"
        empty = '""'
        return self.ch(f"{self.num(sc, d + 1)} == {self.st(sc, d + 1)}", f"null == {self.ch('null', '0', empty)}")

    def anyexpr(self, sc):
        k = self.r.random()
        if k < .55:
            return "n", self.num(sc)
        if k < .80:
            return "s", self.st(sc)
        return "b", self.cond(sc)

    def stmt(self, sc, d, loop, fn):
        r = self.r.random()
        if r < .2:
            self.n += 1
            n, k = f"v{self.n}", self.r.random()
            if k < .1:
                e = "[" + ", ".join(self.num(sc, 2) for _ in range(self.r.randint(1, 3))) + "]"
                sc.append((n, "a"))
            elif k < .15:
                e = '{"k": ' + self.num(sc, 2) + ', "j": ' + self.st(sc, 2) + '}'
                sc.append((n, "o"))
            else:
                t, e = self.anyexpr(sc)
                sc.append((n, t))
            return f"{n} := {e}"
        if r < .32:
            k = self.ch("n", "n", "s", "b")
            x = self.target(sc, k)
            if x:
                return f"{x} = {dict(n=self.num, s=self.st, b=self.cond)[k](sc)}"
        if r < .42:
            x = self.target(sc, "n")
            if x:
                return f"{x} {self.ch('+=', '-=', '*=', '/=', '%=')} {self.num(sc, 2)}"
            x = self.target(sc, "s")
            if x:
                return f"{x} += {self.st(sc, 2)}"
        if r < .47:
            x = self.target(sc, "n")
            if x:
                return f"{x}{self.ch('++', '--')}"
        if r < .52:
            a = self.v(sc, "a")
            if a:
                return self.ch(f"{a}.push({self.num(sc, 2)})", f"{a}[0] = {self.num(sc, 2)}")
        if r < .70:
            return f"print({self.anyexpr(sc)[1]})"
        if r < .80 and d < 3:
            b, e = self.block(list(sc), d + 1, loop, fn), ""
            if self.r.random() < .3:
                e = f" else if ({self.condition(sc)}) {{ {self.block(list(sc), d + 1, loop, fn)} }}"
            if self.r.random() < .4:
                e += " else { " + self.block(list(sc), d + 1, loop, fn) + " }"
            return f"if ({self.condition(sc)}) {{ {b} }}{e}"
        if r < .88 and d < 3:
            self.n += 1
            c = f"c{self.n}"
            b = self.block(list(sc) + [(c, "c")], d + 1, True, fn)
            return f"{c} := 0\nwhile ({c} < {self.r.randint(1, 4)}) {{\n{c} += 1\n{b}\n}}"
        if r < .92 and loop:
            return self.ch("break", "continue") + ("" if self.mid else "\n__END__")
        if r < .95 and (fn or self.r.random() < .15):
            return f"return {self.anyexpr(sc)[1]}"
        return f"print({self.anyexpr(sc)[1]})"

    def block(self, sc, d, loop, fn):
        out = []
        for _ in range(self.r.randint(1, 4)):
            x = self.stmt(sc, d, loop, fn)
            if x.endswith("\n__END__"):
                out.append(x[:-8])
                break
            out.append(x)
        return "\n".join(out)

    def program(self):
        out = []
        for f in range(self.r.randint(0, 3)):
            a = self.r.randint(0, 2)
            name, ps = f"f{f}", [f"q{f}{k}" for k in range(a)]
            sc = [(p, "n") for p in ps]
            body = self.block(sc, 1, False, True)
            out.append(f"fn({name}({', '.join(ps)})) {{\n{body}\nreturn {self.num(sc, 1)}\n}}")
            self.fns.append((name, a))
        if self.r.random() < .15:
            out.append("fn(rec(n)) {\nif (n <= 0) { return 0 }\nreturn rec(n - 1) + n\n}")
            self.fns.append(("rec", 1))
        sc = []
        out.append("\n".join(self.stmt(sc, 0, False, False) for _ in range(self.r.randint(3, 12))))
        return "\n".join(out) + "\n"

    def straight(self):
        """A straight-line block (declarations, assignments, prints) for the
        context family."""
        sc, lines = [], []
        for _ in range(self.r.randint(3, 8)):
            r = self.r.random()
            if r < .4 or not sc:
                self.n += 1
                t, e = self.anyexpr(sc)
                sc.append((f"v{self.n}", t))
                lines.append(f"v{self.n} := {e}")
            elif r < .6:
                k = self.ch("n", "s", "b")
                x = self.target(sc, k)
                lines.append(f"{x} = {dict(n=self.num, s=self.st, b=self.cond)[k](sc)}" if x else
                             f"print({self.anyexpr(sc)[1]})")
            else:
                lines.append(f"print({self.anyexpr(sc)[1]})")
        return lines


def typed(seed, count):
    """count programs of each kind: review fuzz2 (mid-block jumps, zero
    literals), fuzz3 (jumps end their block) and the clean variant of both."""
    for i in range(count):
        for kind, mid, clean in (("mid", True, False), ("end", False, False), ("clean-mid", True, True),
                                 ("clean-end", False, True), ("clean-end2", False, True)):
            src = Typed(random.Random(seed * 1000003 + i * 7 + len(kind)), mid, clean).program()
            yield "typed", f"typed-{kind}-{seed}-{i}", src, {}


CONTEXTS = {
    "top": lambda body: "\n".join(body) + "\n",
    "loop": lambda body: "i0 := 0\nwhile (i0 < 1) {\ni0 += 1\n" + "\n".join(body) + "\n}\n",
    "fn-top": lambda body: "fn(w0()) {\n" + "\n".join(body) + "\nreturn 0\n}\nprint(w0())\n",
    "fn-loop": lambda body: ("fn(w0()) {\n" + "\n".join(body) + "\nreturn 0\n}\n"
                             "i0 := 0\nwhile (i0 < 1) {\ni0 += 1\nprint(w0())\n}\n"),
}


def context(seed, count):
    for i in range(count):
        body = Typed(random.Random(seed * 7907 + i), False, clean=i % 2 == 1).straight()
        for ctx, wrap in CONTEXTS.items():
            yield "context", f"context-{ctx}-{seed}-{i}", wrap(body), {}


NUMBERS = ["0", "1", "7", "0.0", "0.00", "0.000", "0e5", "0.0e0", "0.0e5", "0E0", "1.0", "1.50", "2.5e2", "1e3", "1E3",
           ".5", "5.", "0.5", "1e-7", "1.5e-7", "1e15", "1e16", "1e+3", "9007199254740991", "9007199254740992",
           "1.0e-300", "1e-300", "1e-299", "1e299", "123456789012345.6", "12345678901234.5", "0.1", "3.0e0", "007",
           "1.0E2", "4.5e-1", "100.25", "65535", "2.0"]
NUMBER_USES = [
    ["print({x})"], ["print(-{x})"], ["print(-({x}))"], ['print("" + {x})'], ['print({x} + "")'], ["x1 := {x}", "print(x1)"],
    ["x1 := {x}", "print(-x1)"], ["x1 := {x}", "y1 := x1", "print(y1)"], ["a1 := [{x}]", "print(a1[0])"],
    ['o1 := {{"k": {x}}}', "print(o1.k)"], ["print({x} * 1)"], ["print({x} + 0)"], ["print({x} / 1)"],
    ["print({x} - {x})"], ["print(({x}))"], ["print((true ? {x} : 1))"], ["x1 := {x}", "x1 += 0", "print(x1)"],
    ["print({x} == 0)"], ["print({x} * -1)"],
]


def numeric(_seed, _count):
    for x in NUMBERS:
        for u, use in enumerate(NUMBER_USES):
            body = [line.format(x=x) for line in use]
            for ctx in CONTEXTS:
                yield "numeric", f"numeric-{x}-{u}-{ctx}", CONTEXTS[ctx](body), {}
        yield "numeric", f"numeric-{x}-topreturn", f"return {x}\n", {}
        yield "numeric", f"numeric-{x}-fnreturn", f"fn(z()) {{ return {x} }}\nprint(z())\n", {}


RECEIVERS = ['"abc"', '""', '"héllo"', "5", "1.5", "0.5 - 0.5", "true", "null", "[1, 2]", "[]", '{"k": 1}', "{}",
             '["x"]', '{"k": "v"}']
RECEIVER_OPS = [["print(r.length())"], ["print(r.size())"], ["r.push(1)", "print(r.length())"],
                ['r.push("s")', "print(r.size())"], ["r.push(null)", "print(r[2])"], ["r.push([1])"],
                ["r.push(1, 2)"], ["print(r[0])"], ['print(r["k"])'], ["print(r[1.0])"], ["print(r[-1])"],
                ["print(r[0.5])"], ["print(r.k)"], ["print(r)"], ["print(r + 1)"], ['print(r + "s")'],
                ['print("s" + r)'], ["print(-r)"], ["print(!r)"], ["print(r == 1)"], ["print(r != null)"],
                ["print(r < 1)"], ['print(r >= "a")'], ["print(r && 1)"], ["print(r || 0)"], ["print(r ?? 1)"],
                ["print(r * 2)"], ["print(r % 2)"], ["r += 1", "print(r)"], ['r += "x"', "print(r)"],
                ["r++", "print(r)"], ["r[0] = 9", "print(r[0])"], ['r[0] = "s"', "print(r[0])"], ["return r"]]


def receiver(_seed, _count):
    for x, recv in enumerate(RECEIVERS):
        for o, ops in enumerate(RECEIVER_OPS):
            body = ["r := " + recv] + ops
            for ctx in CONTEXTS:
                yield "receiver", f"receiver-{x}-{o}-{ctx}", CONTEXTS[ctx](body), {}


WORDS = """a b c d e f g h i j k l m n o p q r s t u v w x y z fib rec main test foo bar baz tmp val value values key
keys item items list count total sum avg min max len length size push pop map filter reduce sort reverse join split
first last next prev data result res out input output name title loop content page index id type kind str int num
float bool string number array object null true false args env self this it acc buf line text word char add sub mul
div mod abs floor ceil round sqrt pow exp log now time date random rand seed hash format parse print warn error err
exit quit assert check run call apply bind new copy clone clear empty has get set contains find replace trim upper
lower slice substr range repeat each step go f0 f1 q00 v1 c1 w0 x1 y1 a1 o1""".split()


def legacy_words():
    """Every identifier-shaped string literal in the legacy evaluator source."""
    names = set()
    for pattern in ("src/Parser*.cpp", "src/Ast.cpp", "src/CLI.cpp"):
        for path in sorted(glob.glob(os.path.join(REPO, pattern))):
            names.update(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', open(path, errors="replace").read()))
    return names


NAME_TEMPLATES = [
    ("fn", "fn({w}(a)) {{ return 42 }}\nprint({w}(1))\n"),
    ("fn0", "fn({w}()) {{ return \"u\" }}\ni0 := 0\nwhile (i0 < 1) {{ print({w}()); i0 += 1 }}\n"),
    ("bind", "{w} := 5\nprint({w})\n"),
    ("bindloop", "i0 := 0\nwhile (i0 < 2) {{ {w} := i0; print({w}); i0 += 1 }}\n"),
    ("assign", "{w} := 5\n{w} = 6\nprint({w} + 1)\n"),
    ("param", "fn(g0({w})) {{ return {w} + 1 }}\nprint(g0(1))\n"),
    ("member", "x0 := {{\"{w}\": 1}}\nprint(x0.{w})\nprint(x0[\"{w}\"])\n"),
]


def names(_seed, _count):
    words = sorted((legacy_words() | set(WORDS)) - {"fn", "if", "while", "for", "return", "else"})
    for w in words:
        for label, template in NAME_TEMPLATES:
            yield "names", f"names-{w}-{label}", template.format(w=w), {}


ALIAS_ARGS = ["o.k", "a[0]", "(o.k)", "o[\"k\"]", "n.k[0]", "(true ? o.k : [0])", "o.k ?? [0]", "b", "[1]"]
ALIAS_BODIES = ["p.push(9)", "p[0] = 5", "p = [7]", "q := p\nq.push(1)", "return p", "h(p)", "print(p.length())",
                "print(p[0] + 1)", "t := p.length()\nprint(t)"]
ALIAS_SETUP = 'o := {"k": [1]}\na := [[1]]\nn := {"k": [[1]]}\nb := [1]\n'
ALIAS_AFTER = 'print(o.k.length())\nprint(a[0].length())\nprint(n.k[0].length())\nprint(b.length())\nprint(o.k[0])\n'


def alias(_seed, _count):
    helper = "fn(h(z)) { z.push(8) }\n"
    for i, arg in enumerate(ALIAS_ARGS):
        for j, body in enumerate(ALIAS_BODIES):
            fn = helper + "fn(f(p)) {\n" + body + "\n}\n"
            for ctx in ("top", "loop"):
                call = [f"r0 := f({arg})"] if "return" in body else [f"f({arg})"]
                main = ALIAS_SETUP + (CONTEXTS[ctx](call) if ctx == "top" else CONTEXTS["loop"](call)) + ALIAS_AFTER
                yield "alias", f"alias-{i}-{j}-{ctx}", fn + main, {}
    forms = ["y := o.k", "y := (o.k)", "y := a[0]", "y := (true ? o.k : [0])", "y := o.k ?? [0]", "y := b",
             "y := [o.k]", "y := {\"z\": o.k}", "y := [b]"]
    for i, form in enumerate(forms):
        for mut in ["y.push(2)", "y[0] = 3", "y = [4]"]:
            for ctx in ("top", "loop", "fn-top"):
                body = ALIAS_SETUP.splitlines() + [form, mut, "print(o.k.length())", "print(b.length())",
                                                    "print(o.k[0])"]
                yield "alias", f"alias-decl-{i}-{mut}-{ctx}", CONTEXTS[ctx](body), {}


JUMP_ITEMS = ["print(\"s\")", "k0 += 1", "break", "continue", "return 1", "if (k0 > 1) { break }",
              "if (k0 > 1) { continue }", "if (k0 == 2) { print(\"b\"); break; print(\"x\") }",
              "if (true) { continue; print(\"y\") }", "if (k0 > 2) { return k0 }"]


def control(seed, count):
    r = random.Random(seed * 31337)
    for i in range(count):
        items = [r.choice(JUMP_ITEMS) for _ in range(r.randint(1, 4))]
        inner = "\n".join(items)
        nest = r.random() < .3
        loop = (f"j0 := 0\nwhile (j0 < 2) {{\nj0 += 1\n{inner}\n}}\n" if nest else inner)
        body = f"k0 := 0\nwhile (k0 < 4) {{\nk0 += 1\n{loop}\nprint(k0)\n}}\nprint(\"end\")\n"
        if r.random() < .5:
            yield "control", f"control-{seed}-{i}-top", body, {}
        else:
            yield "control", f"control-{seed}-{i}-fn", f"fn(c0()) {{\n{body}\nreturn 0\n}}\nprint(c0())\n", {}


RECURSION = [
    "fn(d(n)) {{ if (n == 0) {{ return 0 }} return d(n - 1) + 1 }}\nprint(\"start\")\nprint(d({n}))\n",
    "fn(d(n, s)) {{ t := s + \"x\"; if (n == 0) {{ return t.length() }} return d(n - 1, t) }}\nprint(d({n}, \"a\"))\n",
    "fn(d(n, s)) {{ t := s + \"xxxxxxxxxxxxxxxx\"; if (n == 0) {{ return 0 }} return d(n - 1, t) + 1 }}\n"
    "print(d({n}, \"a\"))\n",
    "fn(d(n)) {{ a := [n, n, n]; if (n == 0) {{ return 0 }} return d(n - 1) + a[0] }}\nprint(d({n}))\n",
    "fn(e(n)) {{ if (n == 0) {{ return 0 }} return o(n - 1) }}\nfn(o(n)) {{ if (n == 0) {{ return 1 }} return e(n - 1) }}\n"
    "i0 := 0\nwhile (i0 < 1) {{ print(e({n})); i0 += 1 }}\n",
]


def recursion(_seed, _count):
    for k, template in enumerate(RECURSION):
        for n in (62, 63, 64, 65, 200, 5000, 99998, 100005):
            yield "recursion", f"recursion-{k}-{n}", template.format(n=n), {}


def output(_seed, _count):
    small = {"NIFT_VM_FLUSH": "4096"}
    loop = 'i0 := 0\nwhile (i0 < {n}) {{ print("line " + i0); i0 += 1 }}\n'
    for n in (10, 400, 1000):
        rec = "fn(d(n)) { if (n == 0) { return 0 } return d(n - 1) }\n"
        for head, tail, label in [("", "", "ok"), ("", "print(1 / 0)\n", "bail-error"),
                                  ("", "x9 := 9007199254740991\nprint(x9 + 2)\n", "bail-range"),
                                  (rec, "print(d(70))\n", "recursion"), (rec, "print(d(100005))\n", "budget"),
                                  ("", "return \"done\"\n", "return")]:
            src = head + loop.format(n=n) + tail
            yield "output", f"output-{n}-{label}", src, small
    # Past the real 64 MiB limit: ~70 MiB of output, then a run-time error.
    big = 'p := "' + "x" * 63 + '"\ni0 := 0\nwhile (i0 < 1150000) { print(p); i0 += 1 }\nprint(1 / 0)\n'
    yield "output", "output-64mib-bail", big, {}


TYPING_SEEDS = ["b0 := 2.0", "b0 := 2", "b0 := 2.5", "b0 := \"s\"", "b0 := \"\" + 1.5", "b0 := true", "b0 := null",
                "b0 := 4 / 2", "b0 := 1e3"]
TYPING_DECLS = ["b1 := b0", "b1 := (b0)", "b1 := -b0", "b1 := b0 * 2", "b1 := (b0 * -b0)", "b1 := b0 + 0.5",
                "b1 := b0 + \"\"", "b1 := (true ? b0 : 1)", "b1 := b0 ?? 1", "b1 := b0 / 2", "b1 := b0 % 3",
                "b1 := g0(b0)"]
TYPING_ASSIGNS = ["b1 = 2.75", "b1 = 3", "b1 = \"t\"", "b1 = b0", "b1 = b0 * 1.5", "b1 += 0.5", "b1++", "b1 = null",
                  "b1 = (b0)", "b1 = g0(1.5)"]


def typing(_seed, _count):
    """Binding types derived from declaration text that refers to other
    bindings, then assignments of every kind, in every context (legacy loop
    bodies type such declarations from the value alone)."""
    for i, seed_decl in enumerate(TYPING_SEEDS):
        for j, decl in enumerate(TYPING_DECLS):
            for k, assign in enumerate(TYPING_ASSIGNS):
                body = [seed_decl, decl, assign, "print(b1)"]
                for ctx in CONTEXTS:
                    src = CONTEXTS[ctx](body)
                    src = "fn(g0(x)) { return x }\n" + src
                    yield "typing", f"typing-{i}-{j}-{k}-{ctx}", src, {}


def malformed(seed, count):
    import vm_gen
    for i in range(count):
        src = Typed(random.Random(seed * 524287 + i), i % 2 == 0, clean=True).program()
        text, kind = vm_gen.mutate(src, seed, i)
        yield "malformed", f"malformed-{kind}-{seed}-{i}", text, {}


FAMILIES = {
    "typed": typed, "context": context, "numeric": numeric, "receiver": receiver, "names": names, "alias": alias,
    "control": control, "recursion": recursion, "output": output, "malformed": malformed, "typing": typing,
}

# Review round 2 families (preptype, dupeval, strarg, bytes, memory, deepeq,
# shadow): tests/runtime2/vm_adversarial_gen2.py.
import vm_adversarial_gen2  # noqa: E402

FAMILIES.update(vm_adversarial_gen2.FAMILIES)

import vm_adversarial_gen3  # noqa: E402  (review round 3: elemtarget, cmpchain, deeptree)

FAMILIES.update(vm_adversarial_gen3.FAMILIES)
