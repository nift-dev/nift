#!/usr/bin/env python3
"""Typed, shadowing-heavy random programs (ported from the CP11 round-2
reviewer's generator): a small pool of names re-declared in nested blocks,
loops and loop-called functions with every value kind, assignments of other
kinds, ternaries, method calls and calls between generated functions.
program(rng) returns one program."""
import os
NAMES = ["a", "b", "s", "t", "p", "q", "n"]
KINDS = ["int", "dbl", "str", "bool", "null", "arr", "obj"]
def lit(r, k):
    return r.choice({"int": ["1", "2", "7", "-3", "40", "0"], "dbl": ["2.5", "0.5", "1.25", "-1.5", "1e2", "3.75"],
                     "str": ['"x"', '"yy"', '""', '"7"', "'q'", '"é"', '"a"', '"t"', '"2.5"', '"b * 2.5"'], "bool": ["true", "false"], "null": ["null"],
                     "arr": ["[1, 2]", "[3]", "[1, 2, 3]"], "obj": ['{"k": 1, "j": 2}', '{"k": 5}']}[k])
class G:
    def __init__(self, r):
        self.r = r; self.fns = []; self.nloops = 0; self.cond = False
    def names(self, sc, kinds):
        return [n for n, k in sc.items() if k in kinds]
    def num(self, sc, d):
        r = self.r; c = r.randrange(10)
        ns = self.names(sc, ("int", "dbl", "ctr"))
        if d > 2 or c < 3: return lit(r, r.choice(["int", "dbl"]))
        if c < 5 and ns: return r.choice(ns)
        if c < 7:
            op = r.choice(["+", "-", "*", "+", "-", "%", "/"])
            right = self.num(sc, d + 1)
            if op in "%/" : right = r.choice(["2", "4", "0.5", "3"])
            return f"{self.num(sc, d + 1)} {op} {right}"
        if c < 8:
            a = self.names(sc, ("arr",)); o = self.names(sc, ("obj",)); s = self.names(sc, ("str", "arr"))
            opts = [f"{x}[0]" for x in a] + [f"{x}.k" for x in o] + [f'{x}["k"]' for x in o] + [f"{x}.length()" for x in s]
            if opts: return r.choice(opts)
        if c < 9:
            fs = [f for f in self.fns if f[2] in ("int", "dbl")]
            if fs: return self.call(sc, r.choice(fs), d)
        if self.cond: return lit(r, r.choice(["int", "dbl"]))
        return f"({self.boolean(sc, d + 1)} ? {self.num(sc, d + 1)} : {self.num(sc, d + 1)})"
    def string(self, sc, d):
        r = self.r; c = r.randrange(6); ns = self.names(sc, ("str",))
        if d > 2 or c < 2: return lit(r, "str")
        if c < 4 and ns: return r.choice(ns)
        if c < 5: return f"{self.string(sc, d + 1)} + {r.choice([self.num(sc, d + 1), self.string(sc, d + 1), 'true', 'null'])}"
        fs = [f for f in self.fns if f[2] == "str"]
        if fs: return self.call(sc, r.choice(fs), d)
        return lit(r, "str")
    def cbool(self, sc, d):
        self.cond = True
        try: return self.boolean(sc, d)
        finally: self.cond = False
    def boolean(self, sc, d):
        r = self.r; c = r.randrange(7); ns = self.names(sc, ("bool",))
        if d > 2 or c < 1: return lit(r, "bool")
        if c < 2 and ns: return r.choice(ns)
        if c < 4: return f"{self.num(sc, d + 1)} {r.choice(['<', '<=', '>', '>=', '==', '!='])} {self.num(sc, d + 1)}"
        if c < 5: return f"{self.string(sc, d + 1)} {r.choice(['==', '!=', '<'])} {self.string(sc, d + 1)}"
        if c < 6: return f"({self.boolean(sc, d + 1)} {r.choice(['&&', '||'])} {self.boolean(sc, d + 1)})"
        if d == 0: return f"!({self.boolean(sc, d + 1)})"
        return lit(r, "bool")
    def any(self, sc, k, d=0):
        if k in ("int", "dbl"): return self.num(sc, d)
        if k == "str": return self.string(sc, d)
        if k == "bool": return self.boolean(sc, d)
        if k == "null": return "null"
        ns = self.names(sc, (k,))
        if ns and self.r.random() < 0.3: return self.r.choice(ns)
        return lit(self.r, k)
    def call(self, sc, f, d):
        name, pk, rk = f
        return f"{name}(" + ", ".join(self.any(sc, k, d + 1) for k in pk) + ")"
    def block(self, sc, depth, ind, n, fn, inloop):
        r = self.r; L = []; sc = dict(sc); declared = set(); I = "  " * ind
        for i in range(n):
            c = r.randrange(18)
            if c < 5:
                nm = r.choice([x for x in NAMES if x not in declared] or ["zz"])
                declared.add(nm)
                if r.random() < 0.35 and sc:
                    src = r.choice(list(sc)); k = sc[src]
                    if k == "ctr": k = "int"
                    form = r.choice(["{}", "({})", "{} + 0", "{} * 1"] if k in ("int", "dbl") else ["{}", "({})"])
                    e = form.format(src)
                else:
                    k = r.choice(KINDS); e = self.any(sc, k)
                sc[nm] = k
                L.append(f"{I}{nm} := {e}")
            elif c < 8 and self.names(sc, KINDS):
                nm = r.choice(self.names(sc, KINDS)); k = sc[nm]
                if k in ("int", "dbl") and r.random() < 0.4:
                    L.append(f"{I}{nm} {r.choice(['+=', '-=', '*='])} {self.num(sc, 1)}")
                elif k == "str" and r.random() < 0.3:
                    L.append(f"{I}{nm} += {self.string(sc, 1)}")
                else:
                    k2 = k if r.random() < 0.75 else r.choice(KINDS)
                    if k == "int" and r.random() < 0.3: k2 = "dbl"
                    L.append(f"{I}{nm} = {self.any(sc, k2)}")
            elif c < 11:
                k = r.choice(["int", "dbl", "str", "bool", "null"])
                L.append(f"{I}print({self.any(sc, k)})")
            elif c < 13 and depth < 3:
                self.nloops += 1; cv = f"c{self.nloops}"
                L.append(f"{I}{cv} := 0")
                L.append(f"{I}while({cv} < {r.randint(1, 3)}) {{")
                L.append(f"{I}  {cv} += 1")
                L += self.block(dict(sc, **{cv: "ctr"}), depth + 1, ind + 1, r.randint(1, 5), fn, True)
                if r.random() < 0.3:
                    L.append(f"{I}  if({self.cbool(sc, 1)}) {{ {r.choice(['break', 'continue'])} }}")
                L.append(f"{I}}}")
            elif c < 14 and depth < 3:
                L.append(f"{I}if({self.cbool(sc, 0)}) {{")
                L += self.block(sc, depth + 1, ind + 1, r.randint(1, 3), fn, inloop)
                if r.random() < 0.5:
                    L.append(f"{I}}} else {{")
                    L += self.block(sc, depth + 1, ind + 1, r.randint(1, 3), fn, inloop)
                L.append(f"{I}}}")
            elif c < 16 and self.fns:
                L.append(f"{I}{self.call(sc, r.choice(self.fns), 0)}")
            elif c < 17:
                arrs = self.names(sc, ("arr",))
                if arrs:
                    a = r.choice(arrs)
                    L.append(f"{I}{a}.push({self.num(sc, 1)})" if r.random() < 0.5 else f"{I}{a}[0] = {self.num(sc, 1)}")
            else:
                ns = self.names(sc, ("int", "dbl"))
                if ns: L.append(f"{I}{r.choice(ns)}{r.choice(['++', '--'])}")
        return L
    def program(self):
        r = self.r; src = []
        for f in range(r.randint(0, 3)):
            params = r.sample(NAMES, r.randint(0, 2))
            pk = [r.choice(KINDS) for _ in params]
            rk = r.choice(["int", "dbl", "str", "bool", "null"])
            sc = dict(zip(params, pk))
            body = self.block(sc, 1, 1, r.randint(1, 5), True, False)
            self.cond = os.environ.get("NOTERN") == "1"
            body.append(f"  return {self.any(sc, rk)}")
            self.cond = False
            src.append(f"fn(f{f}({', '.join(params)})) {{"); src += body; src.append("}")
            self.fns.append((f"f{f}", pk, rk))
        src += self.block({}, 0, 0, r.randint(3, 12), False, False)
        return "\n".join(src) + "\n"

def program(rng):
    return G(rng).program()
