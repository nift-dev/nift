"""gen_struct: every receiver built-in (plus a few method names) called on a struct instance in
seven contexts (B5 generator; driven by gate.py).

Moved into the tree by the B5 fix-up (review F6): it was a scratch-only tool. Its only input is the
candidate tree's builtin table (gen1.builtin_table). The fix-up also gives it the same `bad`
classification as gen1 (exit 70 or a timeout, a leaked tag, internal text, dispatch defects): the
scratch version recorded none, so its gate could never fail.
"""
import re

from gen1 import builtin_table
from genrun import vm_vs_legacy

NIFT = None
names = set()
ARGS = ["", '"a"', "1, 1"]
CTX = {
    "top": 'c := S()\nprint("before")\nprint(c.CALL)\nprint("after")\n',
    "for": 'c := S()\nprint("before")\nfor (i : [1]) { print(c.CALL) }\nprint("after")\n',
    "while": 'c := S()\nprint("before")\ni := 0\nwhile (i < 1) { print(c.CALL); i += 1 }\nprint("after")\n',
    "fn": 'fn(g(v)) { return v.CALL }\nprint("before")\nprint(g(S()))\nprint("after")\n',
    "lambda": 'f := (v) => v.CALL\nprint("before")\nprint(f(S()))\nprint("after")\n',
    "stmt": 'c := S()\nprint("before")\nc.CALL\nprint("after")\n',
    "method": 'c := S()\nprint("before")\nprint(c.probe())\nprint("after")\n',
}


def load(tree, nift):
    global NIFT
    NIFT = nift
    text = builtin_table(tree)
    names.clear()
    for m in re.finditer(r'NIFT_RT_BUILTIN\(\w+, "(\w+)", (\w+),', text):
        if m.group(2) != "None":
            names.add(m.group(1))
    names.update({"nosuch", "push", "pop", "sort", "map", "filter", "get", "set", "insert", "remove", "reverse", "keys"})


def items(pass_=None):
    return [(c, n, a) for c in CTX for n in sorted(names) for a in ARGS]


def job(it):
    ctx, name, a = it
    call = f"{name}({a})"
    pre = 'struct(S) {\n  a := 1\n  fn(probe()) { return this.' + call + ' }\n}\n' if ctx == "method" else 'struct(S) { a := 1 }\n'
    src = pre + CTX[ctx].replace("CALL", call)
    r = vm_vs_legacy(NIFT, {"t.f": src})
    vo, ve, vc = r["vm"]
    bad = []
    if vc == 70 or vc >= 124:
        bad.append(f"vm exit {vc}")
    if "\x1f" in vo + ve or "nift:callable" in vo + ve:
        bad.append("tag leak")
    if re.search(r"internal|cell reached|unknown built-in", ve, re.I):
        bad.append("internal text")
    if r["defects"]:
        bad.append(f"defects {r['defects']}")
    diff = True if (not r["refused"] and "legacy" in r and r["legacy"] != r["vm"]) else None
    return {"ctx": ctx, "call": call, "src": src, "refused": r["refused"], "reason": r["reason"], "vm": r["vm"],
            "legacy": r.get("legacy"), "defects": r["defects"], "bad": bad, "diff": diff}
