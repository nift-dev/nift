#!/usr/bin/env python3
"""H-A evaluation-order generator and gate (docs/runtime-2/checkpoints/combined-b3-b5.md).

  gen_evalorder.py --nift ./nift [--tree DIR] [--out DIR] [--jobs N]
  gen_evalorder.py --nift ./nift --negative-controls

Generates every combination of
  operation  (tests/runtime2/b5/gen/evalorder.tsv of --tree: push, insert, remove, splice,
              element store, index read, map set/remove, set add/remove, stack push),
  argument   none / rebind (the argument rebinds the receiver's name) / alias (it mutates the
              receiver through an alias) / drop (it rebinds the name and the alias, so the
              operation's retained receiver is the only reference left) / throw (it rebinds the
              name, then throws: nothing is mutated, the rebinding remains),
  callee     a lambda or a `fn` (a `fn` writes top-level bindings as globals),
  context    top level, a while-loop body, a function's captured local, a top-level binding used
              in a function, the same in a lambda, and a session (`nift -i`),
and runs each on the VM (NIFT_VM=require). Every admitted program must print exactly what the
contract model below computes: the receiver is evaluated first and that value is retained while
the arguments run; then, for an element store, the value before the index. A refusal must be
before any effect (no output). Each admitted program also runs on legacy (NIFT_VM=0); legacy must
agree with the model except in the classes listed in LEGACY_DIFFERENT (legacy's prepared loop
evaluator, legacy's `fn`-callee element store, and legacy's lost receiver when nothing else holds
it), which are reported, not failed. A model/legacy disagreement anywhere else fails the gate:
that is the check that the model is legacy's canonical order.

Fails (exit 1) on any mismatch, VM exit 70, timeout, leaked internal tag, a refused program that
produced output, fewer admitted programs than ADMITTED_FLOOR, or a legacy disagreement outside
LEGACY_DIFFERENT. Exits 2, writing nothing, when the operation table is missing or does not list
exactly the modelled operations. --negative-controls checks both exit-2 cases.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from multiprocessing import Pool

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
TREE_DEFAULT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, HERE)
import genrun  # noqa: E402

TABLE = os.path.join("tests", "runtime2", "b5", "gen", "evalorder.tsv")
INTERNAL = re.compile(r"compiler defect|internal failure|unexpected operands|\x1fnift:|\\u001fnift:|cell reached")
ARGS = ["none", "rebind", "alias", "drop", "throw"]
CALLEES = ["lambda", "fn"]
CONTEXTS = ["top", "loop", "fncell", "fnglob", "lam", "session"]
# Admitted programs on the H-A repair (760 of the 770 generated programs were admitted when the gate
# was written; the 10 refused are `throw` inside a lambda body); fewer means newly refused programs.
ADMITTED_FLOOR = 760

FAMILY = {
    "array": {"init": ["b := [1, 2, 3]"], "new": "[7, 8]", "other": ["keep = [5]"],
              "alias": "keep.push(9)", "show": "%s.stringify()"},
    "map": {"init": ["b := map()", 'b.set("a", 1)'], "new": "map()",
            "other": ["keep = map()", 'keep.set("x", 1)', 'keep.set("y", 2)'], "alias": 'keep.set("z", 9)',
            "show": "%s.size()"},
    "set": {"init": ["b := set()", "b.add(1)"], "new": "set()", "other": ["keep = set()", "keep.add(5)", "keep.add(6)"],
            "alias": "keep.add(9)", "show": "%s.size()"},
    "stack": {"init": ["b := stack()", "b.push(1)"], "new": "stack()",
              "other": ["keep = stack()", "keep.push(5)", "keep.push(6)"], "alias": "keep.push(9)",
              "show": "%s.size()"},
}


class InputMissing(Exception):
    pass


# ---- the contract model -----------------------------------------------------------------------
def initial(family):
    return {"array": [1, 2, 3], "map": {"a": 1}, "set": {1}, "stack": [1]}[family]


def fresh(family):
    return {"array": [7, 8], "map": {}, "set": set(), "stack": []}[family]


def other(family):
    return {"array": [5], "map": {"x": 1, "y": 2}, "set": {5, 6}, "stack": [5, 6]}[family]


def alias_mutation(family, o):
    if family in ("array", "stack"):
        o.append(9)
    elif family == "map":
        o["z"] = 9
    else:
        o.add(9)


def apply_op(op, o):
    """Mutate the retained receiver `o`; return the result binding's value or None."""
    if op == "push_stmt": o.append(4)
    elif op == "push_expr": o.append(4); return 4  # push returns the pushed value
    elif op == "insert_v": o.insert(0, 4)
    elif op == "insert_i": o.insert(0, 9)
    elif op == "remove": return o.pop(0)
    elif op == "splice_s": del o[0:1]
    elif op == "store_v": o[0] = 4
    elif op == "read_k": return o[1]
    elif op == "mset_v": o["k"] = 4
    elif op == "mset_k": o["k"] = 1
    elif op == "mremove": o.pop("a", None)
    elif op == "sadd": o.add(4)
    elif op == "sremove": o.discard(1)
    elif op == "spush": o.append(4)
    return None


MODELLED = ["push_stmt", "push_expr", "insert_v", "insert_i", "remove", "splice_s", "store_v", "read_k",
            "mset_v", "mset_k", "mremove", "sadd", "sremove", "spush"]


def render(family, v):
    if family == "array":
        return "[" + ",".join(str(x) for x in v) + "]"
    return str(len(v))


def expected(op, family, arg, context=None):
    o = initial(family)
    b = keep = o
    # a session shows the value of the collection's initialising statement (set/add/push return 1)
    lines = ["1"] if context == "session" and family != "array" else []
    if arg == "throw":  # a session shows the try statement's value, null
        return lines + ["caught"] + (["null"] if context == "session" else []) + [render(family, fresh(family)),
                                                                                  render(family, o)]
    if arg == "alias":
        alias_mutation(family, o)
    if arg in ("rebind", "drop"):
        b = fresh(family)
    if arg == "drop":
        keep = other(family)
    r = apply_op(op, o)
    if r is not None:
        lines.append(str(r))
    if context == "session" and op == "push_stmt":
        lines.append("4")  # the session shows the statement's value: push returns the pushed value
    return lines + [render(family, b), render(family, keep)]


# ---- program text -----------------------------------------------------------------------------
def load(tree):
    path = os.path.join(tree, TABLE)
    if not os.path.isfile(path):
        raise InputMissing("required input missing: %s" % path)
    rows = []
    for line in open(path):
        if not line.strip() or line.startswith("#"):
            continue
        f = line.rstrip("\n").split("\t")
        if len(f) != 4:
            raise InputMissing("required input missing: malformed row %r in %s" % (line, path))
        rows.append(tuple(f))
    names = [r[0] for r in rows]
    if sorted(names) != sorted(MODELLED):
        raise InputMissing("%s lists only %d of the %d modelled operations (missing %s, extra %s)" % (
            path, len(set(names) & set(MODELLED)), len(MODELLED), sorted(set(MODELLED) - set(names)),
            sorted(set(names) - set(MODELLED))))
    return rows


def callee_def(callee, body):
    if callee == "lambda":
        return "g := () => { %s }" % "; ".join(body)
    return "fn(g()) { %s }" % "; ".join(body)


def program(row, arg, callee, context):
    op, family, template, ret = row
    fam = FAMILY[family]
    body = []
    if arg in ("rebind", "drop", "throw"):
        body.append("b = %s" % fam["new"])
    if arg == "drop":
        body.extend(fam["other"])
    if arg == "alias":
        body.append(fam["alias"])
    body.append('throw error("boom")' if arg == "throw" else "return %s" % ret)
    stmt = template.replace("G", "g()").replace("R :=", "r :=")
    has_result = template.startswith("R ")
    if context == "session" and not has_result and op not in ("push_stmt", "store_v"):
        stmt = "e := " + stmt  # a session shows an expression statement's value: not modelled here
    if arg == "throw":
        op_lines = ['try { %s } catch (e) { print("caught") }' % stmt]
    else:
        op_lines = [stmt] + (["print(r)"] if has_result else [])
    shows = ["print(%s)" % (fam["show"] % "b"), "print(%s)" % (fam["show"] % "keep")]
    setup = fam["init"] + ["keep := b", callee_def(callee, body)]
    if context in ("top", "session"):
        lines = setup + op_lines + shows
    elif context == "loop":
        lines = setup + ["once := 0", "while (once < 1) {", "  once += 1"] + ["  " + l for l in op_lines] + ["}"] + shows
    elif context == "fncell":
        lines = ["fn(t()) {"] + ["  " + l for l in setup + op_lines + shows] + ["  return 0", "}", "t()"]
    elif context == "fnglob":
        lines = setup + ["fn(t()) {"] + ["  " + l for l in op_lines] + ["  return 0", "}", "t()"] + shows
    else:  # lam
        lines = setup + ["t := () => {"] + ["  " + l for l in op_lines] + ["  return 0", "}", "t()"] + shows
    return "\n".join(lines) + "\n"


# Classes where legacy is known to differ from the model (reported, never failed):
#   loop       legacy's prepared loop evaluator reads the receiver late (push, set, add, element
#              store) or loses the mutation (insert, remove, splice, stack push);
#   fn-store   legacy's element store reads the receiver after a value that calls a `fn`;
#   lost       legacy reads a receiver whose last reference the argument dropped (no alias kept);
#   decl-twice legacy evaluates an index key twice in a declaration's initialiser (`r := b[g()]`),
#              observable when the key mutates (here: through the alias); the VM evaluates it once;
#   sharing    approved Phase B differences (arguments/aliases share: none generated here).
def legacy_class(row, arg, callee, context):
    op = row[0]
    if context == "loop":
        return "loop"
    if op == "store_v" and callee == "fn" and arg in ("rebind", "drop", "throw"):
        return "fn-store"
    if arg == "drop":
        return "lost"
    if op == "read_k" and arg == "alias":
        return "decl-twice"
    return None


def items(rows):
    out = []
    for row in rows:
        for arg in ARGS:
            for callee in CALLEES:
                for context in CONTEXTS:
                    if context == "fncell" and callee == "fn":
                        continue  # a `fn` cannot reach a function's locals
                    out.append((row, arg, callee, context))
    return out


NIFT = None


def _init(nift, work):
    global NIFT
    NIFT = nift
    genrun.set_work(work)


def job(item):
    row, arg, callee, context = item
    src = program(row, arg, callee, context)
    want = expected(row[0], row[1], arg, context)
    name = "t.in" if context == "session" else "t.f"
    stdin = src if context == "session" else None
    cli = ["-i"] if context == "session" else None
    files = {} if context == "session" else {name: src}
    vo, ve, vc, rows = genrun.run(NIFT, files, "require", name, stdin, cli)
    refused = "not compiled for the VM" in ve or "session refused" in ve or any(
        r.get("outcome") == "require-error" for r in rows)
    res = {"op": row[0], "arg": arg, "callee": callee, "context": context, "src": src, "want": want,
           "vm": [vo, ve[-400:], vc], "refused": refused}
    if vc == 70 or vc < 0 or vc >= 124 or INTERNAL.search(vo + ve):
        res["bad"] = "internal failure or timeout (exit %d)" % vc
        return res
    if refused:
        if vo.strip():
            res["bad"] = "refused after output"
        return res
    got = vo.splitlines()
    if context == "session":
        got = [l for l in got if l.strip()]
    if vc != 0 or got != want:
        res["bad"] = "result differs from the model"
        return res
    lo, le, lc, _ = genrun.run(NIFT, files, "0", name, stdin, cli)
    lgot = [l for l in lo.splitlines() if l.strip()] if context == "session" else lo.splitlines()
    res["legacy"] = [lo, le[-300:], lc]
    res["legacy_equal"] = lc == 0 and lgot == want
    res["legacy_class"] = legacy_class(row, arg, callee, context)
    if not res["legacy_equal"] and res["legacy_class"] is None:
        res["bad"] = "legacy differs from the model outside the known classes"
    return res


def run_gate(args):
    try:
        rows = load(args.tree)
    except InputMissing as e:
        sys.stderr.write("FAIL evalorder: %s\n" % e)
        return 2
    if not os.path.isfile(args.nift) or not os.access(args.nift, os.X_OK):
        sys.stderr.write("FAIL evalorder: required input missing: nift binary %s\n" % args.nift)
        return 2
    its = items(rows)
    keep = args.out is not None
    out = args.out or tempfile.mkdtemp(prefix="b5-evalorder-")
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="b5-evalorder-work-")
    n = bad = refused = admitted = 0
    legacy_known = {}
    firsts = []
    try:
        with Pool(args.jobs, initializer=_init, initargs=(args.nift, work)) as pool, \
                open(os.path.join(out, "evalorder.jsonl"), "w") as f:
            for res in pool.imap_unordered(job, its, chunksize=8):
                f.write(json.dumps(res) + "\n")
                n += 1
                if res.get("bad"):
                    bad += 1
                    if len(firsts) < 5:
                        firsts.append(res)
                if res["refused"]:
                    refused += 1
                else:
                    admitted += 1
                    if res.get("legacy_class") and not res.get("legacy_equal"):
                        legacy_known[res["legacy_class"]] = legacy_known.get(res["legacy_class"], 0) + 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
        if not keep:
            shutil.rmtree(out, ignore_errors=True)
    ok = bad == 0 and n == len(its) and admitted >= ADMITTED_FLOOR
    print("%s evalorder: %d programs (%d refused, %d admitted; floor %d), %d bad; legacy differs (known classes): %s"
          % ("PASS" if ok else "FAIL", n, refused, admitted, ADMITTED_FLOOR, bad,
             ", ".join("%s %d" % kv for kv in sorted(legacy_known.items())) or "none"), flush=True)
    for res in firsts:
        print("  bad (%s): %s/%s/%s/%s want %s got %r" % (res["bad"], res["op"], res["arg"], res["callee"],
              res["context"], res["want"], res["vm"][:3]))
        print("    " + json.dumps(res["src"])[:400])
    return 0 if ok else 1


def negative_controls(args):
    ok = True
    scratch = tempfile.mkdtemp(prefix="b5-evalorder-neg-")
    try:
        trunc = os.path.join(scratch, "truncated")
        os.makedirs(os.path.dirname(os.path.join(trunc, TABLE)))
        lines = open(os.path.join(TREE_DEFAULT, TABLE)).read().splitlines()
        open(os.path.join(trunc, TABLE), "w").write("\n".join(lines[:-1]) + "\n")  # one operation dropped
        cases = [("missing tree", os.path.join(scratch, "nonexistent"), "required input missing"),
                 ("truncated operation table", trunc, "lists only")]
        for label, tree, text in cases:
            out = os.path.join(scratch, "out-" + label.replace(" ", "-"))
            tmp = os.path.join(scratch, "tmp-" + label.replace(" ", "-"))
            os.makedirs(tmp)
            env = dict(os.environ, TMPDIR=tmp, PYTHONDONTWRITEBYTECODE="1")
            p = subprocess.run([sys.executable, "-I", os.path.abspath(__file__), "--nift", args.nift, "--tree", tree,
                                "--out", out], capture_output=True, text=True, env=env)
            wrote = os.path.exists(out) or bool(os.listdir(tmp))
            good = p.returncode == 2 and text in p.stderr and not p.stdout.strip() and not wrote
            ok = ok and good
            print("%s negative control evalorder (%s): exit %d, %s, written: %s"
                  % ("PASS" if good else "FAIL", label, p.returncode, p.stderr.strip()[:160], wrote))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--tree", default=TREE_DEFAULT)
    ap.add_argument("--out", default=None)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--negative-controls", action="store_true")
    args = ap.parse_args()
    args.nift = os.path.abspath(args.nift)
    if args.negative_controls:
        return negative_controls(args)
    return run_gate(args)


if __name__ == "__main__":
    sys.exit(main())
