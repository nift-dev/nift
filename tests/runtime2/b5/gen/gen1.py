"""gen1: wrong-kind built-in calls on mixed receivers in ten contexts (B5 generator; driven by gate.py).

Independent of b4_dispatch.py / vm_adversarial_gen.py. Moved into the tree by the B5 fix-up (review
F6): it was a scratch-only tool. Its only input is the candidate tree's builtin table
(src/rt/natives/builtin_list.inc); load() raises InputMissing when it is absent or truncated.
Pass "a" (the gate's default): contexts for and fnloop, every receiver set, builtin and arity.
"""
import os
import random
import re

from genrun import vm_vs_legacy

NIFT = None
ar = {}


class InputMissing(Exception):
    pass


def builtin_table(tree):
    src = os.path.join(tree, "src", "rt", "natives", "builtin_list.inc")
    if not os.path.isfile(src):
        raise InputMissing("required input missing: %s (the candidate tree's builtin list)" % src)
    text = open(src).read()
    n = len(re.findall(r'NIFT_RT_BUILTIN\(', text))
    if n < 50:
        raise InputMissing("%s lists only %d builtins (expected the full table)" % (src, n))
    return text


def load(tree, nift):
    global NIFT
    NIFT = nift
    text = builtin_table(tree)
    ar.clear()
    for m in re.finditer(r'NIFT_RT_BUILTIN\(\w+, "(\w+)", (\w+), (\d+), (\w+)', text):
        name, recv, lo, hi = m.group(1), m.group(2), int(m.group(3)), m.group(4)
        if recv == "None":
            continue
        hi = 3 if hi == "kVariadic" else int(hi)
        ar.setdefault(name, set()).update(range(lo, min(hi, 2) + 1))
    for k, v in EXTRA.items():
        ar.setdefault(k, set()).update(v)


EXTRA = {"push": {1}, "pop": {0}, "sort": {0}, "reverse": {0}, "insert": {2}, "remove": {1}, "add": {1},
         "set": {2}, "clear": {0}, "splice": {2}, "map": {1}, "filter": {1}, "reduce": {2}, "find": {1},
         "each": {1}, "wait": {0}, "join": {0}, "count": {0}, "put": {1}, "front": {0}, "write_val": {1}}
ARGS = {0: "", 1: '"a"', 2: '"a", "b"'}
CB = {"map": "(x) => x", "filter": "(x) => true", "find": "(x) => true", "each": "(x) => x", "reduce": "(a, x) => a, 0"}
def argtext(name, n):
    if name in CB: return CB[name]
    if name in ("take", "drop", "chunk", "slice", "substr", "seek", "fetch_add", "fetch_sub", "store", "exchange"):
        return ", ".join(["1"] * n)
    if name in ("compare_exchange",): return "5, 6"
    if name in ("set",) and n == 2: return '"k", 1'
    if name in ("insert", "splice"): return "0, 1"
    return ARGS[n]
KINDS = {
    "null": "null", "bool": "true", "int": "5", "double": "2.5", "string": '" ab "', "array": "[1, 2]",
    "object": '{"a": 1}', "map": "map()", "set": "set()", "stack": "stack()", "struct": "S()",
    "function": "(x) => x", "atomic": "atomic<int>(5)", "mutex": "mutex()", "channel": "channel(1)",
    "future": "w(1)", "file": 'file("o.txt")', "bytes": "bytes()",
}
PRELUDE = 'struct(S) { a := 1\n  fn(touch()) { return 1 } }\nfn[async](w(v)) { return v }\n'
PARTNERS = ["string", "int", "array", "object", "null"]
def recvsets():
    out = [(k,) for k in KINDS]
    for k in KINDS:
        for p in PARTNERS:
            if p != k and (p, k) not in out: out.append((k, p))
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
        return f'fn(mk(i)) {{\n{body}\n  return null\n}}\nprint("before")\nfor (i : {idx}) {{ print(mk(i).{call}) }}\nprint("after")\n'
    if ctx == "branch":
        first = KINDS[ks[0]]; last = KINDS[ks[-1]]
        return f'print("before")\nfor (i : [0, 1]) {{\n  v := {first}\n  if (i == 1) {{ v = {last} }}\n  print(v.{call})\n}}\nprint("after")\n'
    if ctx == "while":
        return f'fn(t(v)) {{ print(v.{call}) }}\nxs := {lst}\ni := 0\nprint("before")\nwhile (i < xs.length()) {{ t(xs[i]); i += 1 }}\nprint("after")\n'
    if ctx == "lambda":
        return f'f := (v) => v.{call}\nprint("before")\nfor (u : {lst}) {{ print(f(u)) }}\nprint("after")\n'
    if ctx == "callback":
        return f'print("before")\nprint({lst}.map((v) => v.{call}))\nprint("after")\n'
    if ctx == "try":
        return f'print("before")\nfor (v : {lst}) {{ try {{ print(v.{call}) }} catch (e) {{ print("caught " + e.message) }} }}\nprint("after")\n'
    if ctx == "objget":
        o = "{" + ", ".join(f'"k{j}": {KINDS[k]}' for j, k in enumerate(ks)) + "}"
        keys = "[" + ", ".join(f'"k{j}"' for j in range(len(ks))) + "]"
        return f'o := {o}\nprint("before")\nfor (k : {keys}) {{ print(o[k].{call}) }}\nprint("after")\n'
    raise ValueError(ctx)
def job(item):
    ctx, ks, name, n = item
    call = f"{name}({argtext(name, n)})"
    src = PRELUDE + context(ctx, ks, call)
    r = vm_vs_legacy(NIFT, {"t.f": src})
    vo, ve, vc = r["vm"]
    bad = []
    if vc == 70 or vc >= 124: bad.append(f"vm exit {vc}")
    if "\x1f" in vo + ve or "nift:callable" in vo + ve: bad.append("tag leak")
    if re.search(r"internal|cell reached|unknown built-in", ve, re.I): bad.append("internal text")
    if r["defects"]: bad.append(f"defects {r['defects']}")
    diff = None
    if not r["refused"] and "legacy" in r and r["legacy"] != r["vm"]:
        diff = True
    return {"ctx": ctx, "kinds": ks, "call": call, "src": src, "refused": r["refused"], "reason": r["reason"],
            "bad": bad, "diff": diff, "vm": r["vm"], "legacy": r.get("legacy")}


def items(pass_="a"):
    ctxs = {"a": ["for", "fnloop"], "b": ["fntop", "ret", "branch", "while", "lambda", "callback", "try", "objget"]}[pass_]
    out = []
    for ctx in ctxs:
        for ks in recvsets():
            for name in sorted(ar):
                for n in sorted(ar[name]):
                    out.append((ctx, ks, name, n))
    if pass_ == "b":
        random.seed(7)
        random.shuffle(out)
        out = out[: int(os.environ.get("NB", "12000"))]
    return out
