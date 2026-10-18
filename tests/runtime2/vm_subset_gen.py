#!/usr/bin/env python3
"""Runtime 2.0 CP11 review round 3 (decision D-016): the generated
supported-subset and unsupported suites.

  vm_subset_gen.py --nift ./nift [--suite supported|unsupported|both]
                   [--seed S] [--count N] [--jobs J] [--report FILE]

VM coverage is a measured outcome, never a threshold maintained by adjusting
test inputs. Three generated suites are kept apart (D-016):

  supported    (this file) random programs built only from the documented
               supported subset (docs/runtime-2/checkpoints/cp11-vm-poc.md,
               "Supported subset (specification)") in every documented
               context: top level, `if` blocks, loops and nested loops,
               functions called from top level, from loops, from `while`
               conditions and from other functions; parameters and returns of
               every value kind; functions with their own loops whose
               `break`/`continue` are reached from outer loops; guarded and
               mutual recursion called from `while` conditions; call results in
               declarations and assignments; element assignment with the
               admitted index forms (a local name, an integer literal); every
               admitted comparison shape (one operator, operands of every
               kind). Every program must run ENTIRELY on the VM (outcome `vm`)
               with legacy's exact stdout, stderr and exit status, except a
               program the generator deliberately ends with a documented
               run-time condition (`print(1 / 0)`, a bad index, ...), which
               must replay with that documented reason. Anything else is a
               failure: a refusal (the subset or the compiler is wrong), an
               unexplained replay, or a difference.
  general      tests/runtime2/vm_parity.py with vm_gen.py (original
               distribution, representative Nift including unsupported
               constructs): reports the VM-execution, replay and fallback
               shares by reason; no threshold.
  unsupported  (this file) a supported program with exactly one construct the
               subset deliberately excludes injected in a random context. It
               must be refused before execution with the documented reason
               (NIFT_VM=classify), and under NIFT_VM=1 the statistics must
               show outcome `fallback` with no VM execution and legacy's
               output.

Deterministic for a seed.
"""
import argparse
import collections
import concurrent.futures as cf
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------
# Legacy's declaration text typing (src/rt/text_type.cpp, ported from
# Parser::expression_type): the generator uses it to keep declarations inside
# the documented subset ("a declaration whose text refers to another binding"
# is excluded in functions and, for names declared inside the loop, in loops).

STRTOD = re.compile(r"^[ \t\n\r\f\v]*[+-]?((\d+\.?\d*|\.\d+)([eE][+-]?\d+)?|0[xX][0-9a-fA-F]+|inf(inity)?|nan)$",
                    re.IGNORECASE)


def _unwrap(t):
    wrapped = True
    while wrapped and len(t) >= 2 and t[0] == "(" and t[-1] == ")":
        wrapped, depth, enclosed = False, 0, True
        for i, c in enumerate(t):
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if (depth == 0 and i + 1 != len(t)) or depth < 0:
                    enclosed = False
                    break
        if enclosed and depth == 0:
            t, wrapped = t[1:-1], True
    return t


def _top_level_binary(t, ops):
    quoted, quote, parens, brackets = False, "", 0, 0
    for i in range(len(t) - 1, -1, -1):
        c = t[i]
        if quoted:
            if c == quote and (i == 0 or t[i - 1] != "\\"):
                quoted = False
            continue
        if c in "'\"":
            quoted, quote = True, c
            continue
        if c == "]":
            brackets += 1
            continue
        if c == "[":
            brackets -= 1 if brackets else 0
            continue
        if brackets:
            continue
        if c == ")":
            parens += 1
            continue
        if c == "(":
            parens -= 1 if parens else 0
            continue
        if parens or c not in ops:
            continue
        if c in "+-" and (i == 0 or t[i - 1] in "+-*/%(<>=!&|?:,"):
            continue
        return i
    return -1


def text_type(text, env):
    """Legacy's text type of `text`: 'n' (none), 'i' (int), 'd' (double) or the
    binding type env[name] of a named binding (functions, true/false/null and
    unknown names: 'n'); `+ - * / %` meet as in meet_text_types."""
    t = text.strip(" \t\r\n")
    if len(t) > 2048:
        return "n"
    t = _unwrap(t)
    if not t:
        return "n"
    if STRTOD.match(t):
        return "d" if re.search(r"[.eE]", t) else "i"
    pos = _top_level_binary(t, "+-")
    if pos < 0:
        pos = _top_level_binary(t, "*/%")
    if pos >= 0:
        left, right = text_type(t[:pos], env), text_type(t[pos + 1:], env)
        if left == "d" or right == "d":
            return "d"
        return "i" if left == "i" and right == "i" else "n"
    if re.fullmatch(r"[A-Za-z0-9_]+", t):
        return env.get(t, "n")
    return "n"


def refers_to_binding(text, functions=()):
    """Does legacy's text type of `text` name a binding (directly or in a +-*/% operand)?"""
    t = text.strip(" \t\r\n")
    if len(t) > 2048:
        return False
    t = _unwrap(t)
    if not t or STRTOD.match(t):
        return False
    pos = _top_level_binary(t, "+-")
    if pos < 0:
        pos = _top_level_binary(t, "*/%")
    if pos >= 0:
        return refers_to_binding(t[:pos], functions) or refers_to_binding(t[pos + 1:], functions)
    if re.fullmatch(r"[A-Za-z0-9_]+", t):
        return t not in ("true", "false", "null") and t not in functions
    return False


# ---------------------------------------------------------------------------
# Supported-subset generator.

ILIT = ["0", "1", "2", "3", "7", "10", "-3", "42", "100"]
DLIT = ["0.5", "1.25", "-0.75", "2.5", ".5", "3.125", "2.5e-1"]  # non-integral binary fractions
SLIT = ['"a"', '"bc"', '""', "'q'", '"x y"', '"é"', '"zz"', '"1"']
CMP = ["<", "<=", ">", ">=", "==", "!="]
KINDS = ["i", "d", "s", "b", "a", "o"]

# Deliberate run-time conditions (documented replay reasons), appended to a
# small share of programs: (statement, expected outcome key).
TRIGGERS = [
    ("print(1 / 0)", "replayed:bail:runtime-error"),
    ('print("a" < 1)', "replayed:bail:runtime-error"),
    ('print(-"a")', "replayed:bail:runtime-error"),
    ("tq := [1]\nprint(tq[5])", "replayed:bail:runtime-error"),
    ('tq := {"k": 1}\nprint(tq.length())', "replayed:bail:unverified"),
    ("tq := 9007199254740991\nprint(tq + 1)", "replayed:bail:number-range"),
    ("tq := [1]\ntr := tq\nprint(tr)", "replayed:bail:aliasing"),
]


class Supported:
    """Types are tracked so that no expression reaches a run-time condition by
    accident: ints stay far below 2^53 and never produce integer -0, doubles
    are non-integral binary fractions (no text/value type disagreement),
    arrays hold ints and have at least one element, objects have the fields
    n (int), x (double), s (string), aggregates are never bound or returned
    from a place, parameters are read-only (array parameters may be pushed),
    `!` never ends the left side of an operator, and functions (all of which
    may be called from loops) use no `?:`, `??` or object literal in an
    `if`/`while` condition or `return`, and declare only from expressions
    whose text names no binding."""

    def __init__(self, rng):
        self.r = rng
        self.n = 0
        self.fns = []   # (name, [param kinds], return kind)
        self.recs = []  # recursive int functions taking one small int
        self.mutual = None
        self.fn_names = set()
        self.in_fn = False
        self.restricted = False  # inside a function condition / return
        self.features = collections.Counter()
        self.bt = {}  # binding name -> legacy binding type (kind letter)

    def name(self, prefix):
        self.n += 1
        return f"{prefix}{self.n}"

    def ch(self, *items):
        return self.r.choice(items)

    @staticmethod
    def par(x):
        return x if re.fullmatch(r"[A-Za-z0-9_.\"'é ]*|.*\)", x) and " " not in x else f"({x})"

    def typed(self, scope, k, d=0):
        """An expression of kind k whose legacy text type agrees with k (none,
        int or k itself), as a declaration, assignment or argument needs."""
        for _ in range(12):
            x = self.e(scope, k, d)
            if text_type(x, self.bt) in ("n", "i", k):
                return x
        return self.literal(k, scope)

    @staticmethod
    def names(scope, kind, mutable=False):
        return [n for n, k, m in scope if k == kind and (m or not mutable)]

    def var(self, scope, kind):
        c = self.names(scope, kind)
        return self.r.choice(c) if c else None

    # ---- expressions by kind
    def e(self, scope, k, d=0, left=False):
        if k == "i":
            return self.ie(scope, d)
        if k == "d":
            return self.de(scope, d)
        if k == "s":
            return self.se(scope, d)
        if k == "b":
            return self.be(scope, d, left)
        if k == "a":
            return self.ae(scope, d)
        return self.oe(scope, d)

    def call(self, scope, kind, d):
        if kind == "i" and self.recs and self.r.random() < 0.3:
            self.features["call:recursive"] += 1
            return f"{self.r.choice(self.recs)}({self.r.randint(0, 9)})"
        c = [f for f in self.fns if f[2] == kind]
        if self.restricted:  # no object literal: object arguments must be variables
            c = [f for f in c if "o" not in f[1] or self.var(scope, "o")]
        if not c:
            return None
        name, params, _ = self.r.choice(c)
        self.features["call:" + kind] += 1
        return f"{name}({', '.join(self.arg(scope, pk, d + 1) for pk in params)})"

    def arg(self, scope, k, d):
        # Places (`a[0]`, `o.n`) only for scalars: parameters are never mutated.
        if k == "i" and self.r.random() < 0.2:
            a, o = self.var(scope, "a"), self.var(scope, "o")
            opts = ([f"{a}[0]"] if a else []) + ([f"{o}.n", f'{o}["n"]'] if o else [])
            if opts:
                self.features["arg:place"] += 1
                return self.r.choice(opts)
        if k == "o" and self.restricted:
            return self.var(scope, "o")
        return self.typed(scope, k, d)

    def ie(self, scope, d):
        r = self.r.random()
        v = self.var(scope, "i")
        if d > 2 or r < 0.3:
            return v if v and self.r.random() < 0.6 else self.r.choice(ILIT)
        if r < 0.45:
            c = self.call(scope, "i", d)
            if c:
                return c
        if r < 0.65:
            op = self.ch("+", "-", "+")
            return f"{self.par(self.ie(scope, d + 1))} {op} {self.par(self.ie(scope, d + 1))}"
        if r < 0.70:
            return f"{self.par(self.ie(scope, d + 1))} * {self.ch('2', '3')}"
        if r < 0.72:
            return f"{self.ch('7', '10', '42')} % {self.ch('3', '4')}"
        if r < 0.84:
            a, o, s = self.var(scope, "a"), self.var(scope, "o"), self.var(scope, "s")
            opts = ([f"{a}.length()", f"{a}.size()", f"{a}[0]", f"{a}[{a}.length() - 1]"] if a else []) + \
                   ([f"{o}.n", f'{o}["n"]'] if o else []) + ([f"{s}.length()"] if s else [])
            if opts:
                self.features["access"] += 1
                return self.r.choice(opts)
        if r < 0.90 and not self.restricted:
            self.features["ternary"] += 1
            return f"({self.be(scope, d + 1, True)} ? {self.ie(scope, d + 1)} : {self.ie(scope, d + 1)})"
        if r < 0.93 and not self.restricted:
            self.features["coalesce"] += 1
            return f"(null ?? {self.ie(scope, d + 1)})"
        return f"({self.ie(scope, d + 1)})"

    def de(self, scope, d):
        r = self.r.random()
        v = self.var(scope, "d")
        if d > 2 or r < 0.35:
            return v if v and self.r.random() < 0.6 else self.r.choice(DLIT)
        if r < 0.45:
            c = self.call(scope, "d", d)
            if c:
                return c
        if r < 0.60:
            return f"{self.par(self.de(scope, d + 1))} {self.ch('+', '-')} {self.par(self.ie(scope, d + 1))}"
        if r < 0.70:
            return f"{self.par(self.de(scope, d + 1))} * 1.5"
        if r < 0.78:
            return f"{self.par(self.de(scope, d + 1))} / 2"
        if r < 0.86:
            o = self.var(scope, "o")
            if o:
                return self.ch(f"{o}.x", f'{o}["x"]')
        return f"({self.de(scope, d + 1)})"

    def se(self, scope, d):
        """No double operand anywhere in a concatenation (its text would type as
        double); the right operand never holds a string variable (bounded
        growth)."""
        r = self.r.random()
        v = self.var(scope, "s")
        if d > 2 or r < 0.35:
            return v if v and self.r.random() < 0.6 else self.r.choice(SLIT)
        if r < 0.45:
            c = self.call(scope, "s", d)
            if c:
                return c
        if r < 0.75:
            right = self.ch(self.r.choice(SLIT), self.par(self.ie(scope, d + 1)), "true", "null")
            return f"{self.r.choice(SLIT) if self.r.random() < 0.3 else self.par(self.se(scope, d + 1))} + {right}"
        if r < 0.85:
            o = self.var(scope, "o")
            if o:
                return f"{o}.s"
        return f"({self.se(scope, d + 1)})"

    def be(self, scope, d, left=False):
        """`!` only as a whole expression at depth 0 (it never ends the left
        side of an operator, which legacy reads differently)."""
        r = self.r.random()
        v = self.var(scope, "b")
        if d > 2 or r < 0.15:
            return v if v and self.r.random() < 0.5 else self.ch("true", "false")
        if r < 0.50:
            return self.comparison(scope, d)
        if r < 0.62:
            op = self.ch("&&", "||")
            self.features["logical"] += 1
            return f"{self.be(scope, d + 1, True)} {op} {self.be(scope, d + 1, True)}"
        if r < 0.68 and d == 0 and not left:
            self.features["not"] += 1
            return f"!({self.be(scope, d + 1)})"
        if r < 0.74:
            c = self.call(scope, "b", d)
            if c:
                return c
        return f"({self.be(scope, d + 1, True)})"

    def comparison(self, scope, d):
        """One comparison; operands are never comparisons themselves."""
        kind = self.r.choice(["i", "i", "d", "s", "b", "a", "o", "mixed", "null"])
        op = self.r.choice(CMP)
        if kind in ("b", "a", "o", "mixed", "null"):
            op = self.ch("==", "!=")
        if kind == "o" and self.restricted:
            kind = "a"
        self.features[f"cmp:{kind}:{op}"] += 1
        if kind == "b":
            return f"{self.var(scope, 'b') or self.ch('true', 'false')} {op} {self.ch('true', 'false')}"
        if kind == "mixed":
            ks = self.r.sample(["i", "d", "s"], 2)
            return f"{self.par(self.e(scope, ks[0], d + 1))} {op} {self.par(self.e(scope, ks[1], d + 1))}"
        if kind == "null":
            return f"{self.par(self.e(scope, self.ch('i', 's'), d + 1))} {op} null"
        return f"{self.par(self.e(scope, kind, d + 1))} {op} {self.par(self.e(scope, kind, d + 1))}"

    def ae(self, scope, d):
        r = self.r.random()
        if r < 0.25:
            c = self.call(scope, "a", d)
            if c:
                return c
        return "[" + ", ".join(self.ie(scope, d + 2) for _ in range(self.r.randint(1, 3))) + "]"

    def oe(self, scope, d):
        if self.restricted:
            return self.var(scope, "o") or "[1]"
        if self.r.random() < 0.3:
            c = self.call(scope, "o", d)
            if c:
                return c
        return ('{"n": ' + self.ie(scope, d + 2) + ', "x": ' + self.de(scope, d + 2) + ', "s": ' +
                self.se(scope, d + 2) + "}")

    def cond(self, scope):
        """`if`/`while` conditions: no `?:`, `??` or object literal anywhere
        (CP10 Opaque in conditions; legacy's prepared parser rejects them)."""
        old = self.restricted
        self.restricted = True
        try:
            return self.be(scope, 0)
        finally:
            self.restricted = old

    # ---- statements
    def decl(self, scope, loop_names):
        k = self.r.choice(KINDS + ["i", "s"])
        v = self.name("v")
        for _ in range(8):
            x = self.typed(scope, k)
            if self.in_fn:
                ok = not refers_to_binding(x, self.fn_names)
            elif loop_names is not None:
                ok = not self._refers_to(x, loop_names)
            else:
                ok = True
            if ok:
                break
        else:
            x = self.literal(k, scope)
        if k in ("a", "o") and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", x):
            x = self.literal(k, scope)  # never bind an aggregate from a place
        scope.append((v, k, True))
        tt = text_type(x, self.bt)
        self.bt[v] = k if tt == "n" else tt
        if loop_names is not None:
            loop_names.add(v)
        self.features["decl:" + k] += 1
        if re.match(r"g\d+\(|r\d+\(|m\d+\(", x):
            self.features["decl:call"] += 1
        return [f"{v} := {x}"]

    def _refers_to(self, text, names):
        """Does the text type name one of `names` (a loop-local binding)?"""
        found = []

        def walk(t):
            t = _unwrap(t.strip(" \t\r\n"))
            if not t or STRTOD.match(t):
                return
            pos = _top_level_binary(t, "+-")
            if pos < 0:
                pos = _top_level_binary(t, "*/%")
            if pos >= 0:
                walk(t[:pos])
                walk(t[pos + 1:])
            elif t in names:
                found.append(t)
        walk(text)
        return bool(found)

    def literal(self, k, scope):
        if k == "i":
            return self.r.choice(ILIT)
        if k == "d":
            return self.r.choice(DLIT)
        if k == "s":
            return self.r.choice(SLIT)
        if k == "b":
            return self.ch("true", "false")
        if k == "a":
            return "[" + ", ".join(self.r.choice(ILIT) for _ in range(self.r.randint(1, 3))) + "]"
        return '{"n": ' + self.r.choice(ILIT) + ', "x": ' + self.r.choice(DLIT) + ', "s": ' + self.r.choice(SLIT) + "}"

    def stmt(self, scope, depth, loop, ret, loop_names):
        r = self.r.random()
        mutable = [(n, k) for n, k, m in scope if m]
        if r < 0.16 or not scope:
            return self.decl(scope, loop_names)
        if r < 0.27 and mutable:
            n, k = self.r.choice(mutable)
            if k in ("a", "o"):
                self.features["assign:" + k] += 1
                return [f"{n} = {self.literal(k, scope) if self.r.random() < 0.5 else self.e(scope, k)}"]
            x = self.typed(scope, k)
            if re.match(r"g\d+\(|r\d+\(|m\d+\(", x):
                self.features["assign:call"] += 1
            self.features["assign:" + k] += 1
            return [f"{n} = {x}"]
        if r < 0.36 and mutable:
            n, k = self.r.choice(mutable)
            if k in "ids":
                self.features["compound:" + k] += 1
            if k == "s":  # legacy: `s += <number or bool>` is an error
                return [f"{n} += {self.r.choice(SLIT)}"]
            if k == "i":
                return [self.ch(f"{n} += {self.par(self.ie(scope, 1))}", f"{n} -= {self.par(self.ie(scope, 1))}", f"{n} *= 2",
                                f"{n}++", f"{n}--", f"++{n}", f"--{n}")]
            if k == "d":
                return [self.ch(f"{n} += {self.par(self.ie(scope, 1))}", f"{n} -= {self.par(self.ie(scope, 1))}", f"{n} *= 1.5",
                                f"{n} /= 2")]
        if r < 0.44:
            a = [n for n, k in mutable if k == "a"]
            if a:
                a = self.r.choice(a)
                x = self.r.random()
                if x < 0.4:
                    self.features["push"] += 1
                    return [f"{a}.push({self.ie(scope, 1)})"]
                ks = [n for n, k, m in scope if k == "k"]
                index = self.r.choice(["0"] + ks)
                self.features["element:" + ("name" if index != "0" else "literal")] += 1
                return [f"{a}[{index}] = {self.ie(scope, 1)}"]
        if r < 0.58:
            self.features["print"] += 1
            return [f"print({self.e(scope, self.r.choice(['i', 'd', 's', 'b', 'b']))})"]
        if r < 0.63 and self.fns:
            c = self.call(scope, self.r.choice(KINDS), 1)
            if c:
                return [c]
        if r < 0.72 and depth < 3:
            self.features["if"] += 1
            out = [f"if ({self.cond(scope)}) {{"] + self.block(scope, depth + 1, loop, ret, loop_names)
            if self.r.random() < 0.3:
                out += [f"}} else if ({self.cond(scope)}) {{"] + self.block(scope, depth + 1, loop, ret, loop_names)
            if self.r.random() < 0.4:
                out += ["} else {"] + self.block(scope, depth + 1, loop, ret, loop_names)
            return out + ["}"]
        if r < 0.80 and depth < 3:
            return self.loop(scope, depth, ret, loop_names)
        if r < 0.86 and loop:
            self.features["jump"] += 1
            self.features["jump:fn" if self.in_fn else "jump:top"] += 1
            return [f"if ({self.cond(scope)}) {{", self.ch("break", "continue"), "}"]
        if r < 0.90 and ret:
            return ["@@return " + self.ret_expr(scope, ret)]
        self.features["print"] += 1
        return [f"print({self.e(scope, self.r.choice(['i', 's', 'b']))})"]

    def loop(self, scope, depth, ret, loop_names):
        c = self.name("c")
        lim = self.r.randint(1, 3)
        cond = f"{c} < {lim}"
        x = self.r.random()
        if x < 0.2 and self.mutual:
            cond = f"{c} < {lim} && {self.mutual}({self.r.randint(0, 6)})"
            self.features["while:mutual"] += 1
        elif x < 0.35 and self.fns:
            old = self.restricted
            self.restricted = True  # a condition
            call = self.call(scope, "i", 2)
            self.restricted = old
            if call:
                cond = f"{c} < {lim} && {call} > -1000000"
                self.features["while:call"] += 1
        inner = set() if loop_names is None else loop_names
        inner.add(c)
        out = [f"{c} := 0", f"while ({cond}) {{", f"{c} += 1"]
        k = self.name("k")
        out.append(f"{k} := 0")
        inner.add(k)
        body_scope = scope + [(c, "c", False), (k, "k", False)]
        self.bt[c] = self.bt[k] = "i"
        self.features["loop:" + ("fn" if self.in_fn else "top") + (":nested" if loop_names is not None else "")] += 1
        out += self.block(body_scope, depth + 1, True, ret, inner)
        return out + ["}"]

    def ret_expr(self, scope, k):
        old = self.restricted
        self.restricted = True
        try:
            x = self.e(scope, k)
            if k in ("a", "o") and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", x):
                x = self.literal(k, scope) if k == "a" else self.call(scope, "o", 1) or "[1]"
            return x
        finally:
            self.restricted = old

    def block(self, scope, depth, loop, ret, loop_names):
        scope = list(scope)
        out = []
        for _ in range(self.r.randint(1, 4)):
            x = self.stmt(scope, depth, loop, ret, loop_names)
            if x and x[-1].startswith("@@"):
                out += x[:-1] + [x[-1][2:]]
                break
            out += x
            if x and x[-1] == "}" and len(x) >= 3 and x[-2] in ("break", "continue"):
                pass
        return out

    def function(self):
        name = self.name("g")
        params = [(self.name("p"), self.r.choice(KINDS)) for _ in range(self.r.randint(0, 3))]
        ret = self.r.choice(KINDS)
        if ret == "o":
            ret = "a"  # object literals are not admitted in returns of loop-called functions
        self.in_fn = True
        scope = [(p, k, False) for p, k in params]  # parameters are read-only
        for p, k in params:
            self.bt[p] = k
        body = self.block(scope, 1, False, ret, None)
        # A pushed array parameter exercises mutation of an argument copy.
        arrays = [p for p, k in params if k == "a"]
        if arrays and self.r.random() < 0.5:
            body.insert(0, f"{arrays[0]}.push({self.r.choice(ILIT)})")
            self.features["param:push"] += 1
        if not body or not body[-1].startswith("return"):
            body.append("return " + self.ret_expr(scope, ret))
        self.in_fn = False
        self.fns.append((name, [k for _, k in params], ret))
        self.fn_names.add(name)
        for _, k in params:
            self.features["param:" + k] += 1
        self.features["return:" + ret] += 1
        return [f"fn({name}({', '.join(p for p, _ in params)})) {{"] + body + ["}"]

    def recursive(self):
        name = self.name("r")
        self.recs.append(name)
        self.fn_names.add(name)
        self.features["recursion"] += 1
        return [f"fn({name}(n)) {{", "if (n <= 0) { return 1 }", f"return {name}(n - 1) + n", "}"]

    def mutual_pair(self):
        a, b = self.name("m"), self.name("m")
        self.mutual = a
        self.fn_names.update((a, b))
        self.features["recursion:mutual"] += 1
        return [f"fn({a}(n)) {{", "if (n == 0) { return true }", f"return {b}(n - 1)", "}",
                f"fn({b}(n)) {{", "if (n == 0) { return false }", f"return {a}(n - 1)", "}"]

    def program(self, trigger=None):
        out = []
        if self.r.random() < 0.4:
            out += self.recursive()
        if self.r.random() < 0.4:
            out += self.mutual_pair()
        for _ in range(self.r.randint(1, 4)):
            out += self.function()
        scope = []
        body = []
        for _ in range(self.r.randint(3, 7)):
            body += self.block(scope, 0, False, None, None)
            if body and body[-1].startswith("return"):
                break
        # Make sure every function is called at least once, from a loop.
        calls = [f"{n}({', '.join(self.arg([], pk, 1) for pk in ps)})" for n, ps, _ in self.fns]
        c = self.name("c")
        loop = [f"{c} := 0", f"while ({c} < 2) {{", f"{c} += 1"] + [f"print({x})" if k in "ids" else x
                                                                    for x, (_, _, k) in zip(calls, self.fns)] + ["}"]
        self.features["loop:calls-every-function"] += 1
        if body and body[-1].startswith("return"):
            body = body[:-1] + loop + [body[-1]]
        else:
            body += loop
            if trigger:
                body.append(trigger)
            elif self.r.random() < 0.05:
                body.append("return " + self.e([], self.ch("i", "s")))
                self.features["top-return"] += 1
        return "\n".join(out + body) + "\n"


def supported_program(seed, index):
    """(source, expected outcome key, features)."""
    rng = random.Random(seed * 7919 + index)
    g = Supported(rng)
    trigger, expect = (None, "vm")
    if rng.random() < 0.06:
        trigger, expect = rng.choice(TRIGGERS)
    src = g.program(trigger)
    if trigger and src.rstrip().split("\n")[-1] != trigger.split("\n")[-1]:
        expect = "vm"  # the program ended in a top-level return first
    return src, expect, g.features


# ---------------------------------------------------------------------------
# Unsupported generator: one excluded construct injected into a supported
# program. (name, functions to add, statements to add, context, reason).
# Contexts: top, if, loop, fn (a new function called from top level), fnloop
# (a new function called from a loop).

INJECT = [
    ("const", "", "const zc := 1\nprint(zc)", "any", "declare-const"),
    ("pattern", "", "[za, zb] := [1, 2]\nprint(za)", "any", "declare-pattern"),
    ("for", "", "for (zx : [1, 2]) { print(zx) }", "any", "statement:For"),
    ("try", "", "try { print(1) } catch (ze) { print(2) }", "any", "statement:Try"),
    ("lambda", "", "zf := (y) => y\nprint(1)", "any", "expression:Lambda"),
    ("bare-expression", "", "zx := 1\nzx", "any", "expression-statement"),
    ("native", "", "print(stringify(1))", "any", "callable:stringify"),
    ("method", "", 'zs := "a,b"\nprint(zs.split(","))', "any", "method:split"),
    ("zero-decimal", "", "print(0.0)", "any", "number-literal"),
    ("big-integer", "", "print(9007199254740993)", "any", "number-literal"),
    ("escape", "", 'print("a\\tb")', "any", "string-literal"),
    ("redeclared", "", "zx := 1\nzx := 2", "any", "static-error:redeclared"),
    ("reserved-binding", "", "title := 1\nprint(title)", "any", "binding-name-reserved"),
    ("member-assign", "", 'zo := {"k": 1}\nzo.k = 2', "any", "static-error:member-assign"),
    ("push-arity", "", "za := [1]\nza.push(1, 2)", "any", "static-error:push-arity"),
    ("comparison-chain", "", "print(1 != 1 == 1)", "any", "comparison-chain"),
    ("comparison-chain-rel", "", "zx := 2\nprint((zx < 3) == true)", "any", "comparison-chain"),
    ("index-target-dot", "", "za := [1, 2]\nza[za.length() - 1] = 9\nprint(za[1])", "any", "index-target"),
    ("index-target-expr", "", "za := [1, 2]\nzi := 0\nza[zi + 1] = 9\nprint(za[1])", "any", "index-target"),
    ("index-compound", "", "za := [1, 2]\nza[0] += 1\nprint(za[0])", "any", "compound-target"),
    ("unresolved", "", "print(args)", "top", "unresolved-name"),
    ("nesting", "", "print(" + " + ".join(["1"] * 400) + ")", "any", "nesting-limit"),
    ("jump-not-last", "", "zi := 0\nwhile (zi < 2) { zi += 1\nbreak\nprint(zi) }", "any", "jump-not-last"),
    ("jump-outside-loop", "", "break", "top", "jump-outside-loop"),
    ("fn-flags", "fn[async](zg()) { return 1 }", "", "top", "fn-flags"),
    ("fn-name-reserved", "fn(is_null(x)) { return 1 }", "", "top", "fn-name-reserved"),
    ("param-name-reserved", "fn(zg(name)) { return 1 }", "print(zg(1))", "top", "param-name-reserved"),
    ("free-name", "fn(zg()) { return zglobal }", "zglobal := 1\nprint(zg())", "top", "free-name"),
    ("arity", "fn(zg(x)) { return x }", "print(zg())", "any", "static-error:arity"),
    ("alias-argument", "fn(zg(p)) { p.push(9)\nreturn 0 }", 'zo := {"k": [1]}\nzg(zo.k)', "any", "alias-argument"),
    ("string-arg-number", "fn(zg(n)) { return n }", 'print(zg("2.5"))', "any", "string-arg-type"),
    ("prepared-decl-fn", "fn(zg(p)) { zq := p\nreturn zq }", "zi := 0\nwhile (zi < 1) { zi += 1\nprint(zg(1)) }",
     "any", "prepared-decl-type"),
    ("prepared-decl-loop", "", "zi := 0\nwhile (zi < 1) { zi += 1\nzx := 2.5\nzy := zx\nprint(zy) }", "any",
     "prepared-decl-type"),
    ("prepared-fn-ternary", "fn(zg(p)) { return (p > 0 ? 1 : 2) }", "zi := 0\nwhile (zi < 1) { zi += 1\nprint(zg(1)) }",
     "any", "prepared-fn-unprepared"),
    ("prepared-fn-bare-return", "fn(zg(p)) { if (p > 0) { return }\nreturn 1 }",
     "zi := 0\nwhile (zi < 1) { zi += 1\nprint(zg(1)) }", "any", "prepared-fn-unprepared"),
    ("bang-precedence", "", "zx := 2\nprint(!zx + 1)", "any", "opaque:irregular-legacy-parse"),
    ("postfix-space", "", "za := [1]\nprint(za [0])", "any", "opaque:legacy-runtime-error"),
]
FN_AFTER = ("fn-after-statement", "print(1)\nfn(zg()) { return 1 }")
CONTEXTS = ["top", "if", "loop", "fn", "fnloop"]


def unsupported_program(seed, index):
    """(source, expected reason prefix, construct name, context)."""
    rng = random.Random(seed * 104729 + index)
    g = Supported(rng)
    base = g.program()
    lines = base.rstrip("\n").split("\n")
    # Split the base into its functions and its statements.
    first = 0
    depth = 0
    for i, line in enumerate(lines):
        if depth == 0 and not line.startswith("fn("):
            first = i
            break
        depth += line.count("{") - line.count("}")
    else:
        first = len(lines)
    fns, stmts = lines[:first], lines[first:]
    if rng.random() < 0.04:
        name, extra = FN_AFTER
        return base + extra + "\n", "fn-after-statement", name, "top"
    name, add_fns, add_stmts, where, reason = rng.choice(INJECT)
    ctx = "top" if where == "top" else rng.choice(CONTEXTS)
    if add_stmts:
        body = add_stmts
        if ctx == "if":
            body = "if (true) {\n" + body + "\n}"
        elif ctx == "loop":
            body = "zl := 0\nwhile (zl < 1) {\nzl += 1\n" + body + "\n}"
        elif ctx in ("fn", "fnloop"):
            add_fns = (add_fns + "\n" if add_fns else "") + "fn(zw()) {\n" + body + "\nreturn 0\n}"
            body = "print(zw())" if ctx == "fn" else "zl := 0\nwhile (zl < 1) {\nzl += 1\nprint(zw())\n}"
    else:
        body = ""
    # Statements go at a top-level statement boundary of the base program.
    cuts = [0]
    depth = 0
    for i, line in enumerate(stmts):
        depth += line.count("{") - line.count("}")
        if depth == 0 and not stmts[i].startswith("return"):
            cuts.append(i + 1)
    cut = rng.choice(cuts)
    if stmts and stmts[-1].startswith("return") and cut == len(stmts):
        cut = len(stmts) - 1
    new = fns + ([add_fns] if add_fns else []) + stmts[:cut] + ([body] if body else []) + stmts[cut:]
    return "\n".join(new) + "\n", reason, name, ctx


# ---------------------------------------------------------------------------
# Runner.

def env_for(cwd, extra=None):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
           "HOME": cwd, "TMPDIR": cwd}
    for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
        if k in os.environ:
            env[k] = os.environ[k]
    env.update(extra or {})
    return env


def run(nift, d, mode, timeout):
    env = env_for(d)
    stats = os.path.join(d, ".stats")
    if os.path.exists(stats):
        os.unlink(stats)
    if mode:
        env.update({"NIFT_VM": mode, "NIFT_VM_STATS": stats})
    try:
        p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL,
                           timeout=timeout)
        res = (p.stdout, p.stderr, p.returncode)
    except subprocess.TimeoutExpired:
        res = (b"", b"timeout", "timeout")
    st = None
    if mode and os.path.exists(stats):
        lines = open(stats).read().splitlines()
        st = json.loads(lines[-1]) if lines else None
    return res, st


def key_of(st):
    if not st:
        return "no-stats"
    return st["outcome"] if st["outcome"] in ("vm", "compiled") else f"{st['outcome']}:{st['reason']}"


def check_supported(nift, work, item, timeout):
    i, src, expect, _ = item
    d = tempfile.mkdtemp(dir=work)
    try:
        open(os.path.join(d, "p.f"), "w").write(src)
        legacy, _ = run(nift, d, None, timeout)
        vm, st = run(nift, d, "1", timeout)
        key = key_of(st)
        problem = None
        if vm != legacy:
            problem = "output differs"
        elif not key.startswith(expect):
            problem = f"expected {expect}, got {key}"
        return i, key, problem, src, legacy, vm
    finally:
        shutil.rmtree(d, ignore_errors=True)


def check_unsupported(nift, work, item, timeout):
    i, src, reason, name, ctx = item
    d = tempfile.mkdtemp(dir=work)
    try:
        open(os.path.join(d, "p.f"), "w").write(src)
        _, cst = run(nift, d, "classify", timeout)
        problem = None
        if not cst or cst["outcome"] != "fallback" or not cst["reason"].startswith(reason):
            problem = f"expected fallback {reason}, got {key_of(cst)}"
            return i, f"{name}/{ctx}", key_of(cst), problem, src, None, None
        legacy, _ = run(nift, d, None, timeout)
        vm, st = run(nift, d, "1", timeout)
        if not st or st["outcome"] != "fallback" or st.get("execute_us", 0) != 0:
            problem = f"not refused before execution ({key_of(st)}, execute_us {st and st.get('execute_us')})"
        elif vm != legacy:
            problem = "fallback output differs from legacy"
        return i, f"{name}/{ctx}", key_of(st), problem, src, legacy, vm
    finally:
        shutil.rmtree(d, ignore_errors=True)


def report_failures(failures):
    for label, problem, src, legacy, vm in failures[:20]:
        print(f"FAIL {label}: {problem}")
        print("  program:\n" + "\n".join("    " + l for l in src.splitlines()[:60]))
        if legacy:
            print(f"  legacy: exit={legacy[2]} out={legacy[0][:300]!r} err={legacy[1][:300]!r}")
            print(f"  vm:     exit={vm[2]} out={vm[0][:300]!r} err={vm[1][:300]!r}")


def suite_supported(args, work, out):
    items, features = [], collections.Counter()
    for i in range(args.count):
        src, expect, feats = supported_program(args.seed, i)
        features.update(feats)
        items.append((i, src, expect, feats))
    tally, failures = collections.Counter(), []
    with cf.ThreadPoolExecutor(args.jobs) as pool:
        for i, key, problem, src, legacy, vm in pool.map(lambda it: check_supported(args.nift, work, it,
                                                                                      args.timeout), items):
            tally[key] += 1
            if problem:
                failures.append((f"supported seed {args.seed} index {i}", problem, src, legacy, vm))
    report_failures(failures)
    vm_n = tally.get("vm", 0)
    replays = {k: n for k, n in tally.items() if k.startswith("replayed")}
    print(f"supported-subset generator: {args.count} programs (seed {args.seed}); outcome vm {vm_n} "
          f"({100.0 * vm_n / max(1, args.count):.1f} %), deliberate replays {sum(replays.values())} {replays}; "
          f"{len(failures)} failure(s)")
    missing = [f for f in REQUIRED_FEATURES if not features.get(f)]
    if args.count >= 200 and missing:
        print(f"FAIL supported-subset generator: features never generated: {missing}")
        failures.append(("features", "missing", "", None, None))
    out["supported"] = {"seed": args.seed, "count": args.count, "outcomes": dict(tally),
                        "failures": len(failures), "features": dict(features)}
    return len(failures)


REQUIRED_FEATURES = [
    "decl:i", "decl:d", "decl:s", "decl:b", "decl:a", "decl:o", "decl:call", "assign:call", "assign:a",
    "compound:i", "compound:d", "compound:s", "push", "element:name", "element:literal", "print", "if",
    "loop:top", "loop:top:nested", "loop:fn", "jump", "while:mutual", "while:call", "recursion",
    "recursion:mutual", "call:recursive", "jump:fn", "jump:top", "arg:place", "param:i", "param:d", "param:s", "param:b", "param:a",
    "param:o", "param:push", "return:i", "return:d", "return:s", "return:b", "return:a", "ternary", "coalesce",
    "logical", "not", "access", "top-return", "loop:calls-every-function",
] + [f"cmp:{k}:{op}" for k in ("i", "d", "s") for op in CMP] + [
    f"cmp:{k}:{op}" for k in ("b", "a", "o", "mixed", "null") for op in ("==", "!=")]


def suite_unsupported(args, work, out):
    items = []
    for i in range(args.count):
        src, reason, name, ctx = unsupported_program(args.seed, i)
        items.append((i, src, reason, name, ctx))
    tally, failures = collections.Counter(), []
    constructs = collections.Counter()
    with cf.ThreadPoolExecutor(args.jobs) as pool:
        for i, label, key, problem, src, legacy, vm in pool.map(
                lambda it: check_unsupported(args.nift, work, it, args.timeout), items):
            tally[key] += 1
            constructs[label.split("/")[0]] += 1
            if problem:
                failures.append((f"unsupported seed {args.seed} index {i} [{label}]", problem, src, legacy, vm))
    report_failures(failures)
    print(f"unsupported generator: {args.count} programs (seed {args.seed}), {len(constructs)} constructs; "
          f"refused before execution with the expected reason: {args.count - len(failures)}; "
          f"{len(failures)} failure(s)")
    out["unsupported"] = {"seed": args.seed, "count": args.count, "outcomes": dict(tally),
                          "constructs": dict(constructs), "failures": len(failures)}
    return len(failures)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--suite", default="both", choices=["supported", "unsupported", "both"])
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--count", type=int, default=400)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--timeout", type=float, default=30)
    ap.add_argument("--report")
    ap.add_argument("--print", type=int, help="print one generated supported program and exit")
    args = ap.parse_args()
    if args.print is not None:
        print(supported_program(args.seed, args.print)[0], end="")
        return 0
    args.nift = os.path.abspath(args.nift)
    work = tempfile.mkdtemp(prefix="nift-vm-subset-gen-")
    out = {}
    try:
        failures = 0
        if args.suite in ("supported", "both"):
            failures += suite_supported(args, work, out)
        if args.suite in ("unsupported", "both"):
            failures += suite_unsupported(args, work, out)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if args.report:
        with open(args.report, "w") as f:
            json.dump(out, f, indent=1, sort_keys=True)
    print(("PASS" if not failures else "FAIL") + f" vm generated subset suites: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
