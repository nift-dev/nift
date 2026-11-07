#!/usr/bin/env python3
"""D2 operand-evaluation-order generator and gate (docs/runtime-2/checkpoints/combined-b3-b5.md
section 27; owner rulings 2026-11-08 03:27/03:29). The target `test-runtime2-b5-evalorder` runs it
after gen_evalorder.py.

  gen_operators.py --nift ./nift [--tree DIR] [--out DIR] [--jobs N]
  gen_operators.py --nift ./nift --negative-controls

Generates every combination of
  operation  (tests/runtime2/b5/gen/operators.tsv of --tree: binary operators, comparisons, the
              fused `if` comparison, string `+`/`==`/`>=`, list/object literals and call arguments,
              compound assignment on a variable (family B) and on a struct field (family C), and
              compound assignment on index targets, which must stay refused),
  callee     the right operand rebinds the left one: through a lambda, a `fn`, or an inline
              assignment,
  context    top level, a while-loop body, a top-level binding in a `fn` and in a lambda, a
              function local captured by a lambda (fn cell), a lambda local captured by an inner
              lambda, a loop binding, a session (`nift -i`), and an uncaptured function local,
and runs each on the VM (NIFT_VM=require). An admitted program must print what the model computes:
operands are evaluated once, left to right; a compound assignment reads its target, then runs the
rhs, then writes back; `/=` giving a double on an int binding fails with legacy's binding-type
error. A refusal must come before any effect. Each admitted program also runs on legacy
(NIFT_VM=0): legacy must agree (an error: the same error line) except in LEGACY_DIFFERENT, which is
reported, not failed.

Fails (exit 1) on any mismatch, VM exit 70, timeout, internal tag, refusal after output, an
admitted index-target compound, fewer admitted programs than ADMITTED_FLOOR, or a legacy
disagreement outside the known classes. Exits 2, writing nothing, when the operation table is
missing or does not list exactly the modelled operations (--negative-controls checks both).
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

TABLE = os.path.join("tests", "runtime2", "b5", "gen", "operators.tsv")
INTERNAL = re.compile(r"compiler defect|internal failure|unexpected operands|\x1fnift:|\\u001fnift:|cell reached|"
                      r"AddressSanitizer|LeakSanitizer|runtime error:")
CONTEXTS = ["top", "loop", "fnglob", "lam", "fncell", "captured", "loopbind", "session", "fnlocal"]
CALLEES = ["lambda", "fn", "inline"]
# Admitted programs on the D2 build (C30); fewer means newly refused programs.
ADMITTED_FLOOR = 795
BINDING_ERROR = "cannot assign double to int binding 'x'"

# family: binding, initial value, the rhs's rebinding value, the rhs's result, their source texts
FAMILY = {
    "num": dict(v="x", init=6, new=20, ret=4, src=("6", "20", "4"), inline="((x = 20) - 16)"),
    "str": dict(v="s", init="a", new="Z", ret="a", src=('"a"', '"Z"', '"a"'),
                inline='((s = "Z") > "" ? "a" : "a")'),
    "dbl": dict(v="x", init=8.0, new=20.0, ret=4, src=("8.0", "20.0", "4"), inline="((x = 20.0) - 16)"),
    "arr": dict(v="b", src=("[6]", "[20]", "4"), inline="((b = [20]).size() + 3)"),
    "obj": dict(v="o", src=('{"k": 6}', '{"k": 20}', "4"), inline='((o = {"k": 20}).size() + 3)'),
    "fld": dict(v="p", init=6, new=20, ret=4, src=("P()", None, "4"), inline="((p.v = 20) - 16)"),
}
HELPERS = ["struct(P) { v := 6 }", "fn(f2(a, c)) { return a }", "l2 := (a, c) => { return a }"]


# ---- the model --------------------------------------------------------------------------------
def fmt(x):
    if isinstance(x, bool):
        return "true" if x else "false"
    if isinstance(x, float):
        return str(int(x)) if x == int(x) else repr(x)
    return str(x)


def _div(a, b):
    return a / b


# value operations: (v, r) -> printed result; `called` False when the right operand never runs
VALUE = {
    "add": lambda v, r: v + r, "sub": lambda v, r: v - r, "mul": lambda v, r: v * r, "div": _div,
    "mod": lambda v, r: v % r, "eq": lambda v, r: v == r + 2, "ne": lambda v, r: v != r + 2,
    "lt": lambda v, r: v < r + 4, "le": lambda v, r: v <= r + 2, "gt": lambda v, r: v > r + 4,
    "ge": lambda v, r: v >= r + 4, "neg_right": lambda v, r: v + -r, "nested_right": lambda v, r: v + r,
    "nested_left": lambda v, r: v + r, "neg_left": lambda v, r: -v + r, "tern_right": lambda v, r: v + r,
    "or_eval": lambda v, r: v == 7 or r == 4, "if_eq": lambda v, r: "T" if v == r + 2 else "F",
    "if_lt": lambda v, r: "T" if v < r + 4 else "F", "list": lambda v, r: "[%d,%d]" % (v, r),
    "objlit": lambda v, r: '{"a":%d,"b":%d}' % (v, r), "call_fn": lambda v, r: v, "call_lam": lambda v, r: v,
    "strcat": lambda v, r: v + r, "str_eq": lambda v, r: v == r, "str_ge": lambda v, r: v >= r,
    "add_dbl": lambda v, r: v + r,
}
COMPOUND = {"c_add": lambda v, r: v + r, "c_sub": lambda v, r: v - r, "c_mul": lambda v, r: v * r,
            "c_div": _div, "c_div_dbl": _div, "c_mod": lambda v, r: v % r, "c_expr": lambda v, r: v + r,
            "c_str": lambda v, r: v + r, "f_add": lambda v, r: v + r, "f_mul": lambda v, r: v * r}
REFUSED = ["c_index", "c_okey"]  # compound on index targets stays refused (out of D2's scope)
MODELLED = sorted(list(VALUE) + ["and_sc"] + list(COMPOUND) + REFUSED)


def expected(op, family, context):
    """("lines", [...]) | ("error", message) | ("refused", None)."""
    if op in REFUSED:
        return "refused", None
    f = FAMILY[family]
    v, new, r = f["init"], f["new"], f["ret"]
    if family == "dbl" and context == "loopbind":
        return "error", BINDING_ERROR  # `for (x : [8.0])` binds an int (legacy's literal rule)
    if op == "and_sc":
        return "lines", ["false", fmt(v)]  # `V == 7` is false: the right operand never runs
    if op in VALUE:
        return "lines", [fmt(VALUE[op](v, r)), fmt(new)]
    result = COMPOUND[op](v, r)
    if op == "c_div" and isinstance(result, float) and result != int(result):
        return "error", BINDING_ERROR
    return "lines", ([fmt(result)] if op == "c_expr" else []) + [fmt(result)]


# Classes where legacy is known to differ from the model (reported, never failed):
#   loop-compound  APPROVED (owner 2026-11-08 03:27/03:29): legacy's prepared loop evaluator reads a
#                  compound target late with a `fn` callee (24) and loses a lambda callee's string
#                  append (`Z`); the VM uses the canonical order (fixture d2/b7). Consequence: an
#                  operation that fails on the target read first (an overflow) now fails on the VM
#                  where legacy, reading the rebinding late, succeeds;
#   loop-args      r05, recorded (compatibility.md): legacy's loop evaluator reads a lambda's call
#                  argument late (fixture d2/b8);
#   no-field-op    legacy cannot run a compound assignment on a struct field (`struct has no method`):
#                  the contract decides (fixture d2/c2).
def legacy_class(op, family, callee, context):
    if family == "fld":
        return "no-field-op"
    if context in ("loop", "loopbind"):
        if op in COMPOUND and (callee == "fn" or family == "str"):
            return "loop-compound"
        if op == "call_lam":
            return "loop-args"
    return None


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
        if len(f) != 3 or f[1] not in FAMILY:
            raise InputMissing("required input missing: malformed row %r in %s" % (line, path))
        rows.append(tuple(f))
    names = [r[0] for r in rows]
    if sorted(names) != MODELLED:
        raise InputMissing("%s lists only %d of the %d modelled operations (missing %s, extra %s)" % (
            path, len(set(names) & set(MODELLED)), len(MODELLED), sorted(set(MODELLED) - set(names)),
            sorted(set(names) - set(MODELLED))))
    return rows


class InputMissing(Exception):
    pass


def program(row, callee, context):
    op, family, template = row
    f = FAMILY[family]
    V = f["v"]
    init, new, ret = f["src"]
    rebind = "p.v = 20" if family == "fld" else "%s = %s" % (V, new)
    body = "%s; return %s" % (rebind, ret)
    g = {"lambda": "g := () => { %s }" % body, "fn": "fn(g()) { %s }" % body, "inline": None}[callee]
    G = f["inline"] if callee == "inline" else "g()"
    show = "print(p.v)" if family == "fld" else ("print(%s.stringify())" % V if family in ("arr", "obj")
                                                  else "print(%s)" % V)
    stmt = [template.replace("V", V).replace("G", G), show]
    decl = "%s := %s" % (V, init)
    defs = [g] if g else []
    ind = lambda ls: ["  " + l for l in ls]  # noqa: E731
    if context in ("top", "session"):
        lines = HELPERS + [decl] + defs + stmt
    elif context == "loop":
        lines = HELPERS + [decl] + defs + ["once := 0", "while (once < 1) {", "  once += 1"] + ind(stmt) + ["}"]
    elif context == "fnglob":
        lines = HELPERS + [decl] + defs + ["fn(t()) {"] + ind(stmt) + ["  return 0", "}", "t()"]
    elif context == "lam":
        lines = HELPERS + [decl] + defs + ["t := () => {"] + ind(stmt) + ["  return 0", "}", "t()"]
    elif context == "fncell":
        lines = HELPERS + ["fn(t()) {"] + ind([decl] + defs + stmt) + ["  return 0", "}", "t()"]
    elif context == "fnlocal":
        lines = HELPERS + ["fn(t()) {"] + ind([decl] + stmt) + ["  return 0", "}", "t()"]
    elif context == "captured":
        lines = HELPERS + ["t := () => {"] + ind([decl] + defs + stmt) + ["  return 0", "}", "t()"]
    else:  # loopbind
        lines = HELPERS + ["for (%s : [%s]) {" % (V, init)] + ind(defs + stmt) + ["}"]
    return "\n".join(lines) + "\n"


def valid(callee, context):
    if context in ("fncell", "captured", "loopbind") and callee == "fn":
        return False  # a `fn` cannot reach a function's or a lambda's locals
    return not (context == "fnlocal" and callee != "inline")


def items(rows):
    return [(row, callee, context) for row in rows for callee in CALLEES for context in CONTEXTS
            if valid(callee, context)]


NIFT = None


def _init(nift, work):
    global NIFT
    NIFT = nift
    genrun.set_work(work)


def errors(err):
    return [l for l in err.splitlines() if l.startswith("error:")]


def outcome_ok(kind, want, out, err, code, session):
    got = [l for l in out.splitlines() if l.strip()] if session else out.splitlines()
    if kind == "error":
        return code == 1 and any(want in l for l in errors(err)) and not got
    if code != 0:
        return False
    if not session:
        return got == want
    k = 0  # a session echoes statement values: the printed lines in order, the last one last
    for line in got:
        if k < len(want) and line == want[k]:
            k += 1
    return k == len(want) and bool(got) and got[-1] == want[-1]


def job(item):
    row, callee, context = item
    op, family = row[0], row[1]
    src = program(row, callee, context)
    kind, want = expected(op, family, context)
    session = context == "session"
    name = "t.in" if session else "t.f"
    stdin, cli, files = (src, ["-i"], {}) if session else (None, None, {name: src})
    vo, ve, vc, rows = genrun.run(NIFT, files, "require", name, stdin, cli)
    refused = "not compiled for the VM" in ve or "session refused" in ve or any(
        r.get("outcome") == "require-error" for r in rows)
    res = {"op": op, "callee": callee, "context": context, "src": src, "want": [kind, want],
           "vm": [vo, ve[-400:], vc], "refused": refused}
    if vc == 70 or vc < 0 or vc >= 124 or INTERNAL.search(vo + ve):
        res["bad"] = "internal failure or timeout (exit %d)" % vc
    elif refused:
        if vo.strip():
            res["bad"] = "refused after output"
    elif kind == "refused":
        res["bad"] = "an index-target compound was admitted (out of scope: must stay refused)"
    elif not outcome_ok(kind, want, vo, ve, vc, session):
        res["bad"] = "result differs from the model"
    else:
        lo, le, lc, _ = genrun.run(NIFT, files, "0", name, stdin, cli)
        res["legacy"] = [lo, le[-300:], lc]
        same = outcome_ok(kind, want, lo, le, lc, session)
        if same and kind == "error" and not session:
            same = errors(le)[:1] == errors(ve)[:1]  # legacy's error line, position included
        res["legacy_equal"] = same
        res["legacy_class"] = legacy_class(op, family, callee, context)
        if not same and res["legacy_class"] is None:
            res["bad"] = "legacy differs from the model outside the known classes"
    return res


def run_gate(args):
    try:
        rows = load(args.tree)
    except InputMissing as e:
        sys.stderr.write("FAIL operators: %s\n" % e)
        return 2
    if not os.path.isfile(args.nift) or not os.access(args.nift, os.X_OK):
        sys.stderr.write("FAIL operators: required input missing: nift binary %s\n" % args.nift)
        return 2
    its = items(rows)
    keep = args.out is not None
    out = args.out or tempfile.mkdtemp(prefix="b5-operators-")
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="b5-operators-work-")
    n = bad = refused = admitted = 0
    known, firsts = {}, []
    try:
        with Pool(args.jobs, initializer=_init, initargs=(args.nift, work)) as pool, \
                open(os.path.join(out, "operators.jsonl"), "w") as f:
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
                        known[res["legacy_class"]] = known.get(res["legacy_class"], 0) + 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
        if not keep:
            shutil.rmtree(out, ignore_errors=True)
    ok = bad == 0 and n == len(its) and admitted >= ADMITTED_FLOOR
    print("%s operators: %d programs (%d refused, %d admitted; floor %d), %d bad; legacy differs (known classes): %s"
          % ("PASS" if ok else "FAIL", n, refused, admitted, ADMITTED_FLOOR, bad,
             ", ".join("%s %d" % kv for kv in sorted(known.items())) or "none"), flush=True)
    for res in firsts:
        print("  bad (%s): %s/%s/%s want %s got %r" % (res["bad"], res["op"], res["callee"], res["context"],
                                                        res["want"], res["vm"][:3]))
        print("    " + json.dumps(res["src"])[:400])
    return 0 if ok else 1


def negative_controls(args):
    ok = True
    scratch = tempfile.mkdtemp(prefix="b5-operators-neg-")
    try:
        trunc = os.path.join(scratch, "truncated")
        os.makedirs(os.path.dirname(os.path.join(trunc, TABLE)))
        lines = open(os.path.join(args.tree, TABLE)).read().splitlines()  # the table of --tree
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
            print("%s negative control operators (%s): exit %d, %s, written: %s"
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
