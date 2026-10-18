#!/usr/bin/env python3
"""Random Nift programs for VM differential testing (Runtime 2.0 CP11).

Programs stay mostly inside the CP11 VM subset (literals, locals, arithmetic,
comparisons, logic, strings, if/while/break/continue, functions, recursion,
arrays/objects with index/member/push/length, print) so that most of them
compile, and they deliberately produce run-time errors (type changes,
division by zero, bad indexes, large numbers) so that bails are exercised.
`mutate` derives near-miss programs (syntax errors, unsupported spellings)
that must fall back to legacy with identical behaviour.

Same seed, same programs. Used by tests/runtime2/vm_parity.py.
"""
import random

# Review round 2: constants small enough that generated values stay within
# +-2^53 (D-018a differences are pinned in vm_golden.py, not generated).
INT_LITS = ["0", "1", "2", "3", "7", "10", "-1", "-5", "100", "65536", "12345", "40000"]
DBL_LITS = ["0.5", "1.5", "2.25", "-0.75", "3.0", "1e3", ".5", "0.1"]
STR_LITS = ['"a"', '"bc"', '""', '"x y"', "'q'", '"héllo"', '"longer than fourteen bytes"', '"0"']


class Gen:
    """Typed generation: variables keep one kind (int, str, arr, obj), so most
    programs run to completion on the VM; `wild` expressions (any operator on
    any operands, arity mistakes) appear with a small probability to exercise
    run-time errors and the bail/replay path."""

    def __init__(self, rng):
        self.r = rng
        self.fns = []      # (name, arity) of int functions with int parameters
        self.counter = 0

    def name(self, prefix):
        self.counter += 1
        return f"{prefix}{self.counter}"

    def pick(self, scope, kind):
        names = [n for n, k in scope if k == kind]
        return self.r.choice(names) if names else None

    # ---- typed expressions
    def int_expr(self, scope, depth=0):
        r = self.r.random()
        if depth == 0 and r < 0.02:
            return self.wild(scope, depth)
        v = self.pick(scope, "int")
        if depth > 2 or r < 0.3:
            return v if v and self.r.random() < 0.7 else self.r.choice(["0", "1", "2", "3", "7", "10", "-4", "100"])
        if r < 0.6:
            op = self.r.choice(["+", "+", "-", "*", "%"])
            right = self.int_expr(scope, depth + 1)
            if op == "%":
                right = self.r.choice(["2", "3", "7"])
            return f"{self.int_expr(scope, depth + 1)} {op} {right}"
        if r < 0.66:
            return f"({self.int_expr(scope, depth + 1)})"
        if r < 0.76 and self.fns:
            name, arity = self.r.choice(self.fns)
            return f"{name}({', '.join(self.small(scope) for _ in range(arity))})"
        a = self.pick(scope, "arr")
        if r < 0.84 and a:
            return self.r.choice([f"{a}.length()", f"{a}[0]", f"{a}.size()"])
        o = self.pick(scope, "obj")
        if r < 0.9 and o:
            return self.r.choice([f"{o}.a", f'{o}["b"]'])
        s = self.pick(scope, "str")
        if r < 0.94 and s:
            return f"{s}.length()"
        return f"({self.cond(scope, depth + 2)} ? {self.int_expr(scope, depth + 1)} : {self.int_expr(scope, depth + 1)})"

    def small(self, scope):
        v = self.pick(scope, "int")
        if v and self.r.random() < 0.5:
            return f"{v} % 10"
        return self.r.choice(["0", "1", "2", "5", "9"])

    def str_expr(self, scope, depth=0):
        r = self.r.random()
        v = self.pick(scope, "str")
        if depth > 2 or r < 0.4:
            return v if v and self.r.random() < 0.6 else self.r.choice(STR_LITS)
        if r < 0.8:
            other = self.r.choice([self.r.choice(STR_LITS), self.int_expr(scope, depth + 1), "true", "null", "1.5"])
            return f"{self.str_expr(scope, depth + 1)} + {other}"
        return f"{self.r.choice(STR_LITS)} + {self.int_expr(scope, depth + 1)}"

    def cond(self, scope, depth=0):
        r = self.r.random()
        if depth > 2 or r < 0.5:
            op = self.r.choice(["<", "<=", ">", ">=", "==", "!="])
            if self.r.random() < 0.8:
                return f"{self.int_expr(scope, depth + 1)} {op} {self.int_expr(scope, depth + 1)}"
            return f"{self.str_expr(scope, depth + 1)} {op} {self.str_expr(scope, depth + 1)}"
        if r < 0.7:
            return f"{self.cond(scope, depth + 1)} {self.r.choice(['&&', '||'])} {self.cond(scope, depth + 1)}"
        if r < 0.8 and depth == 0:
            # Legacy applies `!` to the rest of the unit, so `!` only starts a whole condition.
            return f"!({self.cond(scope, depth + 1)})"
        v = self.pick(scope, "int") or self.pick(scope, "str")
        return v if v else "true"

    def wild(self, scope, depth):
        names = [n for n, _ in scope]
        atom = lambda: self.r.choice(names) if names and self.r.random() < 0.6 else self.literal()
        k = self.r.random()
        if k < 0.4:
            return f"{atom()} {self.r.choice(['+', '-', '*', '/', '%', '??'])} {atom()}"
        if k < 0.6:
            return f"({atom()} {self.r.choice(['<', '==', '&&', '||'])} {atom()})"
        if k < 0.7 and self.fns:
            name, arity = self.r.choice(self.fns)
            return f"{name}({', '.join(atom() for _ in range(arity + self.r.choice([-1, 1]) if arity else 1))})"
        if k < 0.8:
            return f"-{atom()}"
        return atom()

    def literal(self):
        k = self.r.random()
        if k < 0.45:
            return self.r.choice(INT_LITS)
        if k < 0.6:
            return self.r.choice(DBL_LITS)
        if k < 0.8:
            return self.r.choice(STR_LITS)
        return self.r.choice(["true", "false", "null"])

    def value_of(self, kind, scope):
        if kind == "int":
            return self.int_expr(scope)
        if kind == "str":
            return self.str_expr(scope)
        if kind == "arr":
            return f"[{', '.join(self.int_expr(scope) for _ in range(self.r.randint(1, 4)))}]"
        return '{"a": ' + self.int_expr(scope) + ', "b": ' + self.int_expr(scope) + '}'

    # ---- statements
    def block(self, scope, depth, in_loop, in_fn, n):
        lines = []
        local = list(scope)
        for _ in range(n):
            lines.extend(self.statement(local, depth, in_loop, in_fn))
        return lines

    def statement(self, scope, depth, in_loop, in_fn):
        r = self.r.random()
        ind = "  " * depth
        if r < 0.22 or not scope:
            kind = self.r.choice(["int", "int", "int", "str", "arr", "obj"])
            v = self.name("v")
            line = f"{ind}{v} := {self.value_of(kind, scope)}"
            scope.append((v, kind))
            return [line]
        if r < 0.40:
            what = self.r.choice([self.int_expr, self.int_expr, self.str_expr, self.cond])(scope)
            return [f"{ind}print({what})"]
        if r < 0.50:
            name, kind = self.r.choice(scope)
            if self.r.random() < 0.03:
                kind = self.r.choice(["int", "str"])
            return [f"{ind}{name} = {self.value_of(kind, scope)}"]
        if r < 0.58:
            v = self.pick(scope, "int")
            if v:
                op = self.r.choice(["+=", "-=", "*=", "+=", "%="])
                rhs = self.r.choice(["2", "3"]) if op in ("%=", "*=") else self.int_expr(scope)
                return [f"{ind}{v} {op} {rhs}"]
            s = self.pick(scope, "str")
            if s:
                return [f"{ind}{s} += {self.r.choice(STR_LITS)}"]  # no self-append: exponential growth
        if r < 0.62:
            v = self.pick(scope, "int")
            if v:
                return [f"{ind}{v}{self.r.choice(['++', '--'])}"]
        if r < 0.67:
            a = self.pick(scope, "arr")
            if a:
                return [f"{ind}{a}.push({self.int_expr(scope)})"]
        if r < 0.70:
            a = self.pick(scope, "arr")
            if a:
                return [f"{ind}{a}[0] = {self.int_expr(scope)}"]
        if r < 0.80 and depth < 3:
            out = [f"{ind}if({self.cond(scope)}) {{"]
            out += self.block(scope, depth + 1, in_loop, in_fn, self.r.randint(1, 3))
            if self.r.random() < 0.5:
                out.append(f"{ind}}} else if({self.cond(scope)}) {{")
                out += self.block(scope, depth + 1, in_loop, in_fn, self.r.randint(1, 2))
            if self.r.random() < 0.5:
                out.append(f"{ind}}} else {{")
                out += self.block(scope, depth + 1, in_loop, in_fn, self.r.randint(1, 2))
            out.append(f"{ind}}}")
            return out
        if r < 0.88 and depth < 3:
            c = self.name("c")
            out = [f"{ind}{c} := 0", f"{ind}while({c} < {self.r.randint(0, 6)}) {{", f"{ind}  {c} += 1"]
            out += self.block(scope + [(c, "loop")], depth + 1, True, in_fn, self.r.randint(1, 3))
            out.append(f"{ind}}}")
            return out
        if r < 0.92 and in_loop:
            return [f"{ind}if({self.cond(scope)}) {{ {self.r.choice(['break', 'continue'])} }}"]
        if r < 0.95 and in_fn:
            return [f"{ind}return {self.int_expr(scope)}"]
        if r < 0.97 and self.fns:
            name, arity = self.r.choice(self.fns)
            return [f"{ind}{name}({', '.join(self.small(scope) for _ in range(arity))})"]
        return [f"{ind}print({self.int_expr(scope)})"]

    def function(self):
        name = self.name("f")
        arity = self.r.randint(0, 3)
        params = [(self.name("p"), "int") for _ in range(arity)]
        body = self.block(params, 1, False, True, self.r.randint(1, 4))
        body.append(f"  return {self.int_expr(params)}")
        self.fns.append((name, arity))
        return [f"fn({name}({', '.join(p for p, _ in params)})) {{"] + body + ["}"]

    def recursive(self):
        name = self.name("r")
        self.fns.append((name, 1))
        return [f"fn({name}(n)) {{ if(n < 2) {{ return n }} return {name}(n - 1) + {name}(n - 2) }}"]


def program(seed, index):
    rng = random.Random(seed * 1000003 + index)
    g = Gen(rng)
    lines = [f"// vm_gen seed {seed} index {index}"]
    if rng.random() < 0.3:
        lines += g.recursive()
    for _ in range(rng.randint(0, 3)):
        lines += g.function()
    scope = []
    lines += g.block(scope, 0, False, False, rng.randint(3, 12))
    if rng.random() < 0.1:
        lines.append(f"return {g.int_expr(scope)}")
    return "\n".join(lines) + "\n"


MUTATIONS = ["delete", "duplicate", "operator", "space_postfix", "trailing_comma", "dollar", "backslash",
             "swap_lines", "brace", "await", "keyword"]


def mutate(text, seed, index):
    rng = random.Random(seed * 7919 + index)
    kind = rng.choice(MUTATIONS)
    lines = text.split("\n")
    body = [i for i, l in enumerate(lines) if l.strip() and not l.startswith("//")]
    if not body:
        return text, kind
    i = rng.choice(body)
    line = lines[i]
    if kind == "delete" and line:
        k = rng.randrange(len(line))
        lines[i] = line[:k] + line[k + 1:]
    elif kind == "duplicate":
        lines.insert(i, line)
    elif kind == "operator":
        for a, b in (("+", "-"), ("<", "<<"), ("==", "="), (":=", "="), ("(", "((")):
            if a in line:
                lines[i] = line.replace(a, b, 1)
                break
    elif kind == "space_postfix":
        lines[i] = line.replace("[", " [", 1).replace(".length", " .length", 1)
    elif kind == "trailing_comma":
        lines[i] = line.replace("]", ",]", 1).replace(")", ",)", 1)
    elif kind == "dollar":
        lines[i] = line.replace('"a"', '"$[1]"').replace('"bc"', '"$x"')
    elif kind == "backslash":
        lines[i] = line.replace('"a"', '"a\\tb"').replace('"bc"', '"b\\\\c"')
    elif kind == "swap_lines" and len(body) > 1:
        j = rng.choice(body)
        lines[i], lines[j] = lines[j], lines[i]
    elif kind == "brace":
        lines[i] = line + rng.choice(["{", "}", "(", ")"])
    elif kind == "await":
        lines[i] = line.replace("print(", "print(await ", 1)
    elif kind == "keyword":
        lines.insert(i, rng.choice(["const k1 := 1", "for(z : [1]) { print(z) }", "x9 := (a) => a", "#! mid", "o9"]))
    return "\n".join(lines), kind


if __name__ == "__main__":
    import sys
    print(program(int(sys.argv[1]) if len(sys.argv) > 1 else 1, int(sys.argv[2]) if len(sys.argv) > 2 else 0))
