#!/usr/bin/env python3
"""B4b 27: the wrong-kind generators of the B4b reviews (`gen1.py` pass a and pass b, and
`gen_struct.py`, review-b4/repro2), kept as standing program sources. Only the programs are
generated here; the fixed-point gate (fixpoint.py) compiles every one on the fixed-point
builds, and the earlier rounds' differential runs (VM against legacy) used the same programs.

  gen_wrongkind.py --list a|b|struct      print the number of programs of a pass

Every built-in with a receiver (src/rt/natives/builtin_list.inc) is called on values of each
kind (and of each kind and a partner kind) in several contexts: a loop, a function, a returned
value, a branch, a lambda, a callback, a try block, an object member (pass a: `for`, `fnloop`;
pass b: the others, a seeded sample of NB programs, 16,000 by default); `struct` calls every
built-in name (and a few others) on a struct instance in seven contexts."""
import argparse
import os
import random
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SRC = os.path.join(ROOT, "src", "rt", "natives", "builtin_list.inc")

EXTRA = {"push": {1}, "pop": {0}, "sort": {0}, "reverse": {0}, "insert": {2}, "remove": {1}, "add": {1},
         "set": {2}, "clear": {0}, "splice": {2}, "map": {1}, "filter": {1}, "reduce": {2}, "find": {1},
         "each": {1}, "wait": {0}, "join": {0}, "count": {0}, "put": {1}, "front": {0}, "write_val": {1}}
ARGS = {0: "", 1: '"a"', 2: '"a", "b"'}
CB = {"map": "(x) => x", "filter": "(x) => true", "find": "(x) => true", "each": "(x) => x", "reduce": "(a, x) => a, 0"}
KINDS = {
    "null": "null", "bool": "true", "int": "5", "double": "2.5", "string": '" ab "', "array": "[1, 2]",
    "object": '{"a": 1}', "map": "map()", "set": "set()", "stack": "stack()", "struct": "S()",
    "function": "(x) => x", "atomic": "atomic<int>(5)", "mutex": "mutex()", "channel": "channel(1)",
    "future": "w(1)", "file": 'file("o.txt")', "bytes": "bytes()",
}
PRELUDE = 'struct(S) { a := 1\n  fn(touch()) { return 1 } }\nfn[async](w(v)) { return v }\n'
PARTNERS = ["string", "int", "array", "object", "null"]


def arities():
    ar = {}
    for m in re.finditer(r'NIFT_RT_BUILTIN\(\w+, "(\w+)", (\w+), (\d+), (\w+)', open(SRC).read()):
        name, recv, lo, hi = m.group(1), m.group(2), int(m.group(3)), m.group(4)
        if recv == "None":
            continue
        hi = 3 if hi == "kVariadic" else int(hi)
        ar.setdefault(name, set()).update(range(lo, min(hi, 2) + 1))
    for k, v in EXTRA.items():
        ar.setdefault(k, set()).update(v)
    return ar


def argtext(name, n):
    if name in CB:
        return CB[name]
    if name in ("take", "drop", "chunk", "slice", "substr", "seek", "fetch_add", "fetch_sub", "store", "exchange"):
        return ", ".join(["1"] * n)
    if name in ("compare_exchange",):
        return "5, 6"
    if name in ("set",) and n == 2:
        return '"k", 1'
    if name in ("insert", "splice"):
        return "0, 1"
    return ARGS[n]


def recvsets():
    out = [(k,) for k in KINDS]
    for k in KINDS:
        for p in PARTNERS:
            if p != k and (p, k) not in out:
                out.append((k, p))
    return out


def context(ctx, ks, call):
    lst = "[" + ", ".join(KINDS[k] for k in ks) + "]"
    if ctx == "for":
        return f'print("before")\nfor (v : {lst}) {{ print(v.{call}) }}\nprint("after")\n'
    if ctx == "fnloop":
        return f'fn(t(v)) {{ print(v.{call}) }}\nprint("before")\nfor (u : {lst}) {{ t(u) }}\nprint("after")\n'
    if ctx == "fntop":
        calls = "\n".join(f"t({KINDS[k]})" for k in ks)
        return f'fn(t(v)) {{ print(v.{call}) }}\nprint("before")\n{calls}\nprint("after")\n'
    if ctx == "ret":
        body = "\n".join(f"  if (i == {j}) {{ return {KINDS[k]} }}" for j, k in enumerate(ks))
        idx = "[" + ", ".join(str(j) for j in range(len(ks))) + "]"
        return (f'fn(mk(i)) {{\n{body}\n  return null\n}}\nprint("before")\nfor (i : {idx}) {{ print(mk(i).{call}) }}\n'
                f'print("after")\n')
    if ctx == "branch":
        first, last = KINDS[ks[0]], KINDS[ks[-1]]
        return (f'print("before")\nfor (i : [0, 1]) {{\n  v := {first}\n  if (i == 1) {{ v = {last} }}\n'
                f'  print(v.{call})\n}}\nprint("after")\n')
    if ctx == "while":
        return (f'fn(t(v)) {{ print(v.{call}) }}\nxs := {lst}\ni := 0\nprint("before")\n'
                f'while (i < xs.length()) {{ t(xs[i]); i += 1 }}\nprint("after")\n')
    if ctx == "lambda":
        return f'f := (v) => v.{call}\nprint("before")\nfor (u : {lst}) {{ print(f(u)) }}\nprint("after")\n'
    if ctx == "callback":
        return f'print("before")\nprint({lst}.map((v) => v.{call}))\nprint("after")\n'
    if ctx == "try":
        return (f'print("before")\nfor (v : {lst}) {{ try {{ print(v.{call}) }} catch (e) {{ print("caught " + '
                f'e.message) }} }}\nprint("after")\n')
    if ctx == "objget":
        o = "{" + ", ".join(f'"k{j}": {KINDS[k]}' for j, k in enumerate(ks)) + "}"
        keys = "[" + ", ".join(f'"k{j}"' for j in range(len(ks))) + "]"
        return f'o := {o}\nprint("before")\nfor (k : {keys}) {{ print(o[k].{call}) }}\nprint("after")\n'
    raise ValueError(ctx)


def gen1(which, nb=16000):
    """The `gen1.py` programs of pass `which` (a or b), in the reviewer's order."""
    ar = arities()
    ctxs = {"a": ["for", "fnloop"], "b": ["fntop", "ret", "branch", "while", "lambda", "callback", "try",
                                         "objget"]}[which]
    items = []
    for ctx in ctxs:
        for ks in recvsets():
            for name in sorted(ar):
                for n in sorted(ar[name]):
                    items.append((ctx, ks, name, n))
    if which == "b":
        random.seed(7)
        random.shuffle(items)
        items = items[:nb]
    return [PRELUDE + context(ctx, ks, f"{name}({argtext(name, n)})") for ctx, ks, name, n in items]


STRUCT_CTX = {
    "top": 'c := S()\nprint("before")\nprint(c.CALL)\nprint("after")\n',
    "for": 'c := S()\nprint("before")\nfor (i : [1]) { print(c.CALL) }\nprint("after")\n',
    "while": 'c := S()\nprint("before")\ni := 0\nwhile (i < 1) { print(c.CALL); i += 1 }\nprint("after")\n',
    "fn": 'fn(g(v)) { return v.CALL }\nprint("before")\nprint(g(S()))\nprint("after")\n',
    "lambda": 'f := (v) => v.CALL\nprint("before")\nprint(f(S()))\nprint("after")\n',
    "stmt": 'c := S()\nprint("before")\nc.CALL\nprint("after")\n',
    "method": 'c := S()\nprint("before")\nprint(c.probe())\nprint("after")\n',
}


def gen_struct():
    """The `gen_struct.py` programs."""
    names = set()
    for m in re.finditer(r'NIFT_RT_BUILTIN\(\w+, "(\w+)", (\w+),', open(SRC).read()):
        if m.group(2) != "None":
            names.add(m.group(1))
    names |= {"nosuch", "push", "pop", "sort", "map", "filter", "get", "set", "insert", "remove", "reverse", "keys"}
    out = []
    for ctx in STRUCT_CTX:
        for name in sorted(names):
            for a in ["", '"a"', "1, 1"]:
                call = f"{name}({a})"
                pre = ('struct(S) {\n  a := 1\n  fn(probe()) { return this.' + call + ' }\n}\n' if ctx == "method"
                       else 'struct(S) { a := 1 }\n')
                out.append(pre + STRUCT_CTX[ctx].replace("CALL", call))
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", choices=["a", "b", "struct"], required=True)
    args = ap.parse_args()
    print(len(gen_struct() if args.list == "struct" else gen1(args.list)))
