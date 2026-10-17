#!/usr/bin/env python3
"""Runtime 2.0 CP11 review round 2: adversarial families for the VM parity
gate (registered in vm_adversarial_gen.FAMILIES). Ported from the round-2
reviewer's generators; deterministic for a seed. Each yields (family, name,
source, env) or (family, name, source, env, expect), where `expect` is an
outcome-key prefix the VM statistics must show (vm_adversarial.py checks it):

  preptype   M1/N1: a declaration typed from another binding (`b := a`,
             `(a)`, `a + 0`, `a * 1`) in loop bodies, loop-called functions
             (parameter, local, nested caller scopes), with an outer binding
             of every value kind shadowed by an inner one of another kind,
             then every kind of assignment.
  dupeval    M2: functions whose `return`/`if`/`while` text legacy's prepared
             parser rejects (ternaries, chained comparisons, object literals
             with string values, parenthesised `??`, bare `return`) called with
             side-effecting arguments from loops, nested loops, conditions
             and loop-called wrappers.
  strarg     M3: string-literal arguments whose text is a number, an
             expression, or a binding name (global, caller local, parameter,
             undeclared), to parameters that are assigned, compound-assigned,
             passed on or copied, in every context.
  bytes      M4: control and whitespace bytes (VT, FF, CR, NEL, NBSP, U+2028/9,
             BOM, NUL, DEL, other C0) at every position of small programs, and
             whole-program whitespace transforms.
  memory     R5: recursion holding nested values (arrays of arrays, objects,
             distinct growing strings) per frame under a small budget
             (NIFT_VM_HEAP_BUDGET); expected outcome: a recursion-budget bail.
  deepeq     R6: `==`/`!=` on nested arrays/objects around a small equality
             depth (NIFT_VM_EQUALITY_DEPTH=50); deeper must bail.
  shadow     typed random programs with heavy shadowing (the reviewer's
             typed generator): loops, loop-called functions, all value kinds.
"""
import random

WRAP = {
    "top": lambda b: b,
    "loop": lambda b: "i0 := 0\nwhile(i0 < 1) {\ni0 += 1\n" + b + "}\n",
    "fn": lambda b: "fn(w0()) {\n" + b + "return 0\n}\nprint(w0())\n",
    "fnloop": lambda b: "fn(w0()) {\n" + b + "return 0\n}\ni0 := 0\nwhile(i0 < 1) {\ni0 += 1\nprint(w0())\n}\n",
}

PREP_OUTER = ['"s"', "true", "null", "1", "2.5", "[1]", '{"k": 1}', None]
PREP_INNER = ["2.5", "1", '"t"', "false", "3.5 - 1", "2.0"]
PREP_DECL = ["b := a", "b := (a)", "b := a + 0", "b := a * 1"]
PREP_ASSIGN = ["b = 1.5", "b = 2", 'b = "u"', "b = true", "b += 1", "b = null"]


def preptype(_seed, _count):
    n = 0
    for o in PREP_OUTER:
        for inn in PREP_INNER:
            for dcl in PREP_DECL:
                for asg in PREP_ASSIGN:
                    pre = "" if o is None else f"a := {o}\n"
                    body = f"{dcl}\n{asg}\nprint(b)\n"
                    progs = {
                        "loop": pre + f"i := 0\nwhile(i < 1) {{\ni += 1\na := {inn}\n{body}}}\n",
                        "loop-outer": f"a := {inn}\n" + f"i := 0\nwhile(i < 2) {{\ni += 1\n{body}}}\n",
                        "fnparam-loop": f"fn(f(a)) {{\n{body}return 0\n}}\n" + pre +
                                        f"i := 0\nwhile(i < 1) {{\ni += 1\nprint(f({inn}))\n}}\n",
                        "fnlocal-loop": f"fn(f()) {{\na := {inn}\n{body}return 0\n}}\n" + pre +
                                        "i := 0\nwhile(i < 1) {\ni += 1\nprint(f())\n}\n",
                        "fnparam-top": f"fn(f(a)) {{\n{body}return 0\n}}\n" + pre + f"print(f({inn}))\n",
                        "nested-fn": f"fn(f(a)) {{\n{body}return 0\n}}\nfn(g(a)) {{\nreturn f({inn})\n}}\n" + pre +
                                     "i := 0\nwhile(i < 1) {\ni += 1\nprint(g(7))\n}\n",
                        "fn-own-loop": f"fn(f(a)) {{\nk := 0\nwhile(k < 1) {{\nk += 1\n{body}}}\nreturn 0\n}}\n" + pre +
                                       f"print(f({inn}))\n",
                    }
                    for k, src in progs.items():
                        n += 1
                        yield "preptype", f"preptype-{n}-{k}", src, {}


DUP_G = 'fn(g(x)) {\n  print("g" + x)\n  return x\n}\n'
DUP_H = "fn(h(z)) {\n  return (true ? z : 0)\n}\n"
DUP_BODIES = {
    "tern-ret": "return (true ? p : 0)", "tern-decl": "y := (p > 0 ? 1 : 2)\nreturn y",
    "tern-print": 'print((p > 0 ? "a" : "b"))\nreturn p', "obj-str": 'o := {"k": "v"}\nreturn p',
    "squote": "s := 'q'\nreturn p", "plain": "return p", "plain2": "y := p + 1\nreturn y",
    "if-tern": "if(p > 0) { print((true ? 1 : 2)) }\nreturn p",
    "while-tern": "i := 0\nwhile(i < 1) { i += 1\nprint((true ? 1 : 2)) }\nreturn p", "nested-call-tern": "return h(p)",
    "chaincmp": "print(1 < 2 == true)\nreturn p", "notprec": "print(!(p == 1))\nreturn p", "coal": "return p ?? 0",
    "paren-coal": "return (p ?? 0)", "member": 'o := {"k": p}\nreturn o.k', "arr": "a := [p, 1]\nreturn a[0]",
    "strcat": 'print("x" + p)\nreturn p', "dbl": "y := 1.5\nreturn p", "bare": "if(p > 5) { return }\nreturn p",
    "if-chain": "if(p < 2 == true) { print(1) }\nreturn 0", "while-chain": "while(p < 2 == false) { print(1) }\nreturn 0",
    "obj-ret": 'return {"k": "v"}.k', "obj-if": 'if({"k": "v"}.k == "v") { print(1) }\nreturn 0',
    "ret-chain": "return p >= 1 != false",
}
DUP_CALLS = {
    "stmt": "f(g(1))", "decl": "r := f(g(1))", "print": "print(f(g(1)))", "assign": "r0 = f(g(1))",
    "arg2": "print(f(g(1)) + f(g(2)))", "argvar": "v := 1\nf(v)", "cond": 'if(f(g(1)) > 0) { print("t") }',
    "nested": "f(f(g(1)))",
}
DUP_CTX = {
    "top": lambda s: "r0 := 0\n" + s + "\n",
    "loop": lambda s: "r0 := 0\nc := 0\nwhile(c < 2) {\nc += 1\n" + s + "\n}\n",
    "loop2": lambda s: "r0 := 0\nc := 0\nwhile(c < 1) {\nc += 1\nd := 0\nwhile(d < 1) {\nd += 1\n" + s + "\n}\n}\n",
    "if": lambda s: "r0 := 0\nif(true) {\n" + s + "\n}\n",
    "while-cond": lambda s: "c := 0\nwhile(f(g(c)) < 1) { c += 1 }\nprint(c)\n",
}


def dupeval(_seed, _count):
    for bn, body in DUP_BODIES.items():
        fn = DUP_G + DUP_H + "fn(f(p)) {\n" + body + "\n}\n"
        for cn, call in DUP_CALLS.items():
            for xn, wrap in DUP_CTX.items():
                if xn == "while-cond" and cn != "stmt":
                    continue
                yield "dupeval", f"dupeval-{bn}-{cn}-{xn}", fn + wrap(call), {}
            src = fn + "fn(w()) {\nr0 := 0\n" + call + "\nreturn 0\n}\nc := 0\nwhile(c < 2) {\nc += 1\nw()\n}\n"
            yield "dupeval", f"dupeval-{bn}-{cn}-fnloop", src, {}


STR_CONTENTS = ["q", "2.5", "7", "1e3", "b * 2.5", "x + 1", "zz", "p", "2.0", "", " ", "q.k", "(q)", "1 + 0.5",
                "hello world", "s2", "n"]
STR_USES = {
    "assign-str": "s = \"7\"\nreturn 0", "assign-num": "s = 1\nreturn 0", "assign-dbl": "s = 1.5\nreturn 0",
    "compound": "s += \"x\"\nreturn s", "copy": "t := s\nt = \"z\"\nreturn t", "pass": "return h(s)",
    "read": "return s",
}
STR_SETUPS = {
    "none": "", "global-null": "q := null\n", "global-dbl": "q := 2.5\nx := 1.5\n", "global-str": 'q := "a"\nzz := 3\n',
}


def strarg(_seed, _count):
    h = "fn(h(v)) {\nv = \"w\"\nreturn v\n}\n"
    for c in STR_CONTENTS:
        for un, use in STR_USES.items():
            fn = h + "fn(f(s)) {\n" + use + "\n}\n"
            for sn, setup in STR_SETUPS.items():
                call = f'print(f("{c}"))\n'
                for xn in ("top", "loop"):
                    body = setup + (call if xn == "top" else "i0 := 0\nwhile(i0 < 1) {\ni0 += 1\n" + call + "}\n")
                    yield "strarg", f"strarg-{c}-{un}-{sn}-{xn}", fn + body, {}
            # A caller's local or parameter named like the text.
            src = fn + f'fn(g(q)) {{\np := 1\nreturn f("{c}")\n}}\nprint(g(2.5))\n'
            yield "strarg", f"strarg-{c}-{un}-caller", src, {}


BYTES = ["\v", "\f", "\r", "\t", "\x1c", "\x1f", "\x00", "\x7f", "\u0085", " ", " ", " ", "﻿",
         "\x01", "\x1b"]
BYTE_TEMPLATES = [
    "x := 1{c}\nprint(x)\n", "x := 1\n{c}print(x)\n", "x := 1{c}\nx = 2\nprint(x)\n", "print(1){c}\n",
    "print(1 +{c}2)\n", "print({c}1)\n", "x{c} := 1\nprint(x)\n", "x := 1\nif(x == 1) {{{c}print(2) }}\n",
    "x := 1\nif(x == 1) {{ print(2) {c}}}\n", "x := 1\nwhile(x < 3) {{ x += 1{c} }}\nprint(x)\n",
    "fn(f(a)) {{ return a{c} }}\nprint(f(1))\n", "fn(f(a{c})) {{ return a }}\nprint(f(1))\n",
    "a := [1,{c}2]\nprint(a[1])\n", 'o := {{"k":{c}1}}\nprint(o.k)\n', "x := 1\nx++{c}\nprint(x)\n",
    "x := 1\nx += 2{c}\nprint(x)\n", "x := 1\ni := 0\nwhile(i < 1) {{\ni += 1\nx := 2.5{c}\nprint(x)\n}}\n",
    "return 5{c}\n", "x := true{c}\nprint(x)\n", 'x := "a"{c}\nprint(x)\n', "x := (1){c}\nprint(x)\n",
    "x := 1\ny := x{c}\nprint(y)\n", "print(1){c};print(2)\n", "{c}\nprint(1)\n", "if (1 <{c}2) {{ print(1) }}\n",
]
WS_BASE = [
    'fn(add(a, b)) { return a + b }\nx := 1.5\ny := add(x, 2)\nprint(y)\ni := 0\nwhile(i < 3) {\n  if(i == 1) { i += 1\n'
    ' continue }\n  print(i)\n  i += 1\n}\ns := "a b"\nprint(s + "!")\nprint(s.length())\n',
    'fn(fib(n)) {\n  if(n < 2) { return n }\n  return fib(n - 1) + fib(n - 2)\n}\nprint(fib(10))\nt := 0\nk := 0\n'
    'while(k < 5) { t += k; k++ }\nprint(t)\nreturn t\n',
]
WS_TRANSFORMS = {
    "crlf": lambda s: s.replace("\n", "\r\n"), "cr": lambda s: s.replace("\n", "\r"), "bom": lambda s: "﻿" + s,
    "semi": lambda s: s.replace("\n", ";\n"), "trail": lambda s: s.replace("\n", "  \t\n"),
    "ff": lambda s: s.replace("\n", "\n\f"), "vt": lambda s: s.replace("\n", "\v\n"),
    "ff-before-nl": lambda s: s.replace("\n", "\f\n"), "nbsp": lambda s: s.replace(" := ", " := ", 1),
    "nul": lambda s: s + "\0", "tabsep": lambda s: s.replace(" ", "\t"),
}


def bytes_family(_seed, _count):
    for i, c in enumerate(BYTES):
        for j, t in enumerate(BYTE_TEMPLATES):
            yield "bytes", f"bytes-{i}-{j}", t.format(c=c), {}
    r = random.Random(3)
    names = list(WS_TRANSFORMS)
    for bi, b in enumerate(WS_BASE):
        for tn, t in WS_TRANSFORMS.items():
            yield "bytes", f"bytes-ws-{bi}-{tn}", t(b), {}
        for k in range(20):
            s = b
            picked = r.sample(names, 3)
            for tn in picked:
                s = WS_TRANSFORMS[tn](s)
            yield "bytes", f"bytes-ws-{bi}-{'+'.join(picked)}-{k}", s, {}


MEMORY_HOLDERS = {
    "nested-array": "[[s]]", "array": "[s, s]", "object": '{"k": [s]}', "deep": "[[[[s]]]]", "string": "s",
    "object-of-objects": '{"a": {"b": {"c": s}}}',
}


def memory(_seed, _count):
    """Every frame holds a distinct ~16 KB string inside the holder; with a
    2 MiB budget that is exceeded only well past legacy's 64 frames."""
    env = {"NIFT_VM_HEAP_BUDGET": str(2 << 20)}
    for hn, holder in MEMORY_HOLDERS.items():
        for grow in ("double", "append"):
            build = ("k := 0\nwhile(k < 10) { s += s; k += 1 }\n" if grow == "double"
                     else 'k := 0\nwhile(k < 1000) { s += "0123456789abcdef"; k += 1 }\n')
            src = (f'fn(mk(n)) {{\ns := "B" + n\n{build}return {holder}\n}}\n'
                   "fn(f(n)) {\nif(n == 0) { return 0 }\nw := mk(n)\nreturn f(n - 1) + 1\n}\n"
                   'print("start")\nprint(f(3000))\n')
            yield "memory", f"memory-{hn}-{grow}", src, env, "replayed:bail:recursion-budget"
    # Within the budget and within legacy's depth: runs on the VM.
    yield "memory", "memory-shallow", ('fn(f(n)) { s := "x" + n\nif(n == 0) { return 0 }\nreturn f(n - 1) + 1 }\n'
                                       "print(f(50))\n"), env, "vm"


def deepeq(_seed, _count):
    env = {"NIFT_VM_EQUALITY_DEPTH": "50"}
    shapes = {"array": ("fn(w(p)) { return [p ?? 0] }\n", "[1]"),
              "object": ('fn(w(p)) { return {"k": p ?? 0} }\n', "{}"),
              "pair": ("fn(w(p)) { return [0, p ?? 0] }\n", "[1]")}
    for sn, (fn, seed) in shapes.items():
        for depth in (10, 49, 50, 51, 60, 200):
            for leaf, op in (("1", "=="), ("2", "=="), ("1", "!=")):
                y_seed = seed.replace("1", leaf) if seed != "{}" else ('{"z": 1}' if leaf == "2" else "{}")
                src = (fn + f"x := {seed}\ny := {y_seed}\ni := 0\n"
                       f"while(i < {depth}) {{ x = w(x); y = w(y); i += 1 }}\nprint(x {op} y)\nprint(x {op} x)\n")
                expect = "replayed:bail:nesting-depth" if depth >= 50 else "vm"
                yield "deepeq", f"deepeq-{sn}-{depth}-{leaf}-{op}", src, env, expect


def shadow(seed, count):
    import vm_shadow_gen
    for i in range(count * 2):
        # Seed 1 is the reviewer's seed 4 (eight differences in its first 6,000).
        yield "shadow", f"shadow-{seed}-{i}", vm_shadow_gen.program(random.Random((seed + 3) * 100003 + i)), {}


FAMILIES = {"preptype": preptype, "dupeval": dupeval, "strarg": strarg, "bytes": bytes_family, "memory": memory,
            "deepeq": deepeq, "shadow": shadow}
