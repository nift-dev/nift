#!/usr/bin/env python3
"""B4b alias generator (the B4b reviewer's, kept as a standing test): a string slot reachable by
two paths is replaced through one path (in some context: top level, functions, closures,
callbacks, thread, async, a function given a receiver-returning method's result) and observed through the other with a string-only method. B4b 32: holders whose second path is a receiver-returning method's result
(reverse(), sort(), a comparator sort), and contexts passing such a result to a function.

  gen_alias.py --nift NIFT [--sample N] [--seed S] [--out OUT.jsonl] [--legacy] [--min-vm N]
  gen_alias.py --nift NIFT --negative-controls

Fails on any exit 70, internal text, internal tag or dispatch defect (on the guard-off build too:
there the VM shares references, so a stale element type reaches an unflagged native and shows as
`compiler defect`). --legacy also counts the differences from legacy (informational). --min-vm N
also fails when fewer than N programs run on the VM (the production gate, test-runtime2-b4b-gen-alias:
a binary that refuses everything must not pass). Every program comes from this file's templates.
--negative-controls runs the generator on two stub binaries (one exits 70, one refuses every
program) and requires both runs to fail."""
import argparse, json, os, random, re, shutil, subprocess, sys, tempfile
from multiprocessing import Pool


def run(nift, src, mode, work):
    d = tempfile.mkdtemp(dir=work)
    open(os.path.join(d, "t.f"), "w").write(src)
    st = os.path.join(d, "s.jsonl")
    env = {"PATH": "/usr/bin:/bin", "HOME": d, "TMPDIR": d, "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
           "NO_COLOR": "1", "NIFT_VM": mode, "NIFT_VM_STATS": st}
    p = subprocess.run(["timeout", "30", nift, "t.f"], cwd=d, env=env, stdin=subprocess.DEVNULL, capture_output=True)
    rows = []
    if os.path.exists(st):
        for line in open(st):
            try:
                rows.append(json.loads(line))
            except ValueError:
                pass
    norm = lambda b: b.decode("utf-8", "replace").replace(d + os.sep, "")
    shutil.rmtree(d, ignore_errors=True)
    return norm(p.stdout), norm(p.stderr), p.returncode, rows


ARGS = None
S = '" a "'
PRE = 'struct(C) {\n  a := 1\n}\n'
# name: (setup, mutation root, slot (array/object element), array holding the slot or None, observation)
T = {
 "alias":      ('xs := [S]\nys := xs\n', 'ys', '{R}[0]', '{R}', 'xs[0]'),
 "alias_rev":  ('xs := [S]\nys := xs\n', 'xs', '{R}[0]', '{R}', 'ys[0]'),
 "member":     ('o := {"k": S}\np := o\n', 'p', '{R}.k', None, 'o.k'),
 "member_idx": ('o := {"k": S}\np := o\n', 'p', '{R}["k"]', None, 'o.k'),
 "inner":      ('xs := [[S]]\ninner := xs[0]\n', 'inner', '{R}[0]', '{R}', 'xs[0][0]'),
 "inner_rev":  ('xs := [[S]]\ninner := xs[0]\n', 'xs', '{R}[0][0]', None, 'inner[0]'),
 "aoa":        ('xs := [{"k": [S]}]\na := xs[0].k\n', 'a', '{R}[0]', '{R}', 'xs[0].k[0]'),
 "aoa_rev":    ('xs := [{"k": [S]}]\na := xs[0].k\n', 'xs', '{R}[0].k[0]', None, 'a[0]'),
 "oao":        ('o := {"a": [{"k": S}]}\nq := o.a[0]\n', 'q', '{R}.k', None, 'o.a[0].k'),
 "oao_rev":    ('o := {"a": [{"k": S}]}\nq := o.a[0]\n', 'o', '{R}.a[0].k', None, 'q.k'),
 "three":      ('xs := [S]\no := {"in": xs}\narr := [o]\n', 'arr', '{R}[0].in[0]', None, 'xs[0]'),
 "three_rev":  ('xs := [S]\no := {"in": xs}\narr := [o]\n', 'xs', '{R}[0]', '{R}', 'arr[0].in[0]'),
 "mapget":     ('xs := [S]\nm := map()\nm.set("k", xs)\nys := m.get("k")\n', 'ys', '{R}[0]', '{R}', 'xs[0]'),
 "loopvar":    ('xs := [[S]]\n', 'xs', None, None, 'xs[0][0]'),
 # B4b 32 (review of ce0a9a44, N-1): the second path is the result of a receiver-returning method
 # (reverse(), sort(), a comparator sort: the result is the receiver itself), directly, chained
 # through aliases, of a nested element, or returned by a function or a lambda.
 "rev":        ('xs := [S]\nys := xs.reverse()\n', 'ys', '{R}[0]', '{R}', 'xs[0]'),
 "rev_back":   ('xs := [S]\nys := xs.reverse()\n', 'xs', '{R}[0]', '{R}', 'ys[0]'),
 "sort":       ('xs := [S]\nys := xs.sort()\n', 'ys', '{R}[0]', '{R}', 'xs[0]'),
 "csort":      ('xs := [S]\nys := xs.sort((p, q) => true)\n', 'ys', '{R}[0]', '{R}', 'xs[0]'),
 "rev_chain":  ('xs := [S]\nys := xs.reverse()\nzs := ys.sort()\n', 'zs', '{R}[0]', '{R}', 'xs[0]'),
 "rev_inner":  ('xs := [[S]]\ninner := xs[0]\nys := inner.reverse()\n', 'ys', '{R}[0]', '{R}', 'xs[0][0]'),
 "rev_outer":  ('xs := [[S]]\nys := xs.sort((p, q) => true)\n', 'ys', '{R}[0][0]', None, 'xs[0][0]'),
 "rev_fnret":  ('fn(rv(a)) { return a.reverse() }\nxs := [S]\nys := rv(xs)\n', 'ys', '{R}[0]', '{R}', 'xs[0]'),
 "sort_lam":   ('rl := (a) => a.sort()\nxs := [S]\nys := rl(xs)\n', 'ys', '{R}[0]', '{R}', 'xs[0]'),
}
# the holders whose mutation root is an array: the receiver-returning contexts apply to them
RECEIVER_ROOT = {"alias", "alias_rev", "inner", "aoa", "three_rev", "mapget", "rev", "rev_back", "sort", "csort",
                 "rev_chain", "rev_inner", "rev_fnret", "sort_lam"}
V = {"int": "5", "array": "[1]", "map": "map()", "struct": "C()", "object": '{"z": 1}'}
def mutation(form, slot, arr, R, v):
    if form == "set": return (slot.replace("{R}", R) + " = " + v) if slot else None
    if form == "insert": return (arr.replace("{R}", R) + ".insert(0, " + v + ")") if arr else None
    if form == "splice": return (arr.replace("{R}", R) + ".splice(0, 1, [" + v + "])") if arr else None
    return None
def ctx(c, setup, root, mut):
    body = mut.replace("\n", "\n  ")
    if c == "top": return setup + mut + "\n"
    if c == "fn": return setup + "fn(f(a)) {\n  " + body.replace(root, "a", 1) + "\n  return 0\n}\nf(" + root + ")\n"
    if c == "global_fn": return setup + "fn(f()) {\n  " + body + "\n  return 0\n}\nf()\n"
    if c == "closure": return setup + "g := () => {\n  " + body + "\n  return 0\n}\ng()\n"
    if c == "ret_closure": return setup + "fn(mk(a)) {\n  return () => {\n    " + body.replace(root, "a", 1) + "\n    return 0\n  }\n}\nh := mk(" + root + ")\nh()\n"
    if c == "obj_fn": return setup + "fo := {\"f\": (a) => {\n  " + body.replace(root, "a", 1) + "\n  return 0\n}}\nfo.f(" + root + ")\n"
    if c == "map_cb": return setup + "rs := [" + root + "].map((a) => {\n  " + body.replace(root, "a", 1) + "\n  return 0\n})\n"
    if c == "filter_cb": return setup + "rs := [" + root + "].filter((a) => {\n  " + body.replace(root, "a", 1) + "\n  return true\n})\n"
    if c == "sort_cb": return setup + "rs := [" + root + ", " + root + "]\nrs.sort((a, b) => {\n  " + body.replace(root, "a", 1) + "\n  return false\n})\n"
    if c == "apply": return setup + "fn(app(fx, v)) { return fx(v) }\napp((a) => {\n  " + body.replace(root, "a", 1) + "\n  return 0\n}, " + root + ")\n"
    if c == "stored_fn": return setup + "fs := (a) => {\n  " + body.replace(root, "a", 1) + "\n  return 0\n}\nhold := {\"g\": fs}\nk := hold.g\nk(" + root + ")\n"
    if c == "reduce_cb": return setup + "r := [1].reduce((acc, x) => {\n  " + body.replace(root, "acc", 1) + "\n  return acc\n}, " + root + ")\n"
    if c == "thread": return setup + "fn(w(a)) {\n  " + body.replace(root, "a", 1) + "\n  return 0\n}\nt := thread(w, " + root + ")\nt.join()\n"
    if c == "async": return setup + "fn[async](w(a)) {\n  " + body.replace(root, "a", 1) + "\n  return 0\n}\nr := await w(" + root + ")\n"
    # B4b 32: the mutated array reaches a function as a receiver-returning method's result
    if c in ("rev_arg", "sort_arg", "csort_arg"):
        call = {"rev_arg": ".reverse()", "sort_arg": ".sort()", "csort_arg": ".sort((p, q) => true)"}[c]
        return setup + "fn(f(a)) {\n  " + body.replace(root, "a", 1) + "\n  return 0\n}\nf(" + root + call + ")\n"
    raise ValueError(c)
CTX = ["top", "fn", "global_fn", "closure", "ret_closure", "obj_fn", "map_cb", "filter_cb", "sort_cb", "apply",
       "stored_fn", "reduce_cb", "thread", "async", "rev_arg", "sort_arg", "csort_arg"]
def obs(o, path):
    if o == "top": return 'print("before")\nprint(' + path + '.trim())\n'
    if o == "loop": return 'print("before")\nfor (i : [1]) { print(' + path + '.trim()) }\n'
    if o == "fnobs": return 'fn(rd()) { return ' + path + '.trim() }\nprint("before")\nprint(rd())\n'
    if o == "early_fn": return None
    raise ValueError(o)
def items():
    out = []
    for name, (setup, root, slot, arr, path) in T.items():
        setup = setup.replace("S", S) if name != "loopvar" else setup.replace("S", S)
        for form in ["set", "insert", "splice"]:
            for vk, v in V.items():
                if name == "loopvar":
                    if form != "set": continue
                    mut = "for (x : " + root + ") {\n  x[0] = " + v + "\n}"
                else:
                    mut = mutation(form, slot, arr, root, v)
                if mut is None: continue
                for c in CTX:
                    if c.endswith("_arg") and name not in RECEIVER_ROOT: continue
                    for o in ["top", "loop", "fnobs"]:
                        src = PRE + ctx(c, setup, root, mut) + obs(o, path)
                        out.append({"t": name, "form": form, "v": vk, "ctx": c, "obs": o, "src": src})
        # early observer: a function compiled before the store reads the slot (fnobs defined first)
    return out
def job(it):
    work = tempfile.mkdtemp(prefix="b4b-alias-")
    try:
        vo, ve, vc, rows = run(ARGS.nift, it["src"], "require", work)
        refused = any(r.get("outcome") == "require-error" for r in rows) or "not compiled for the VM" in ve
        bad = []
        if vc == 70 or vc >= 124 or vc < 0: bad.append("vm exit %d" % vc)
        if "\x1f" in vo + ve: bad.append("tag leak")
        if re.search(r"internal failure|compiler defect|unexpected operands|cell reached|unknown built-in", ve, re.I):
            bad.append("internal text")
        defects = sum(int(r.get("dispatch_defects", 0)) for r in rows)
        if defects: bad.append("defects %d" % defects)
        it.update({"refused": refused, "bad": bad, "vm": [vo, ve, vc]})
        if ARGS.legacy and not refused:
            it["diff"] = list(run(ARGS.nift, it["src"], "0", work)[:3]) != [vo, ve, vc]
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return it


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out")
    ap.add_argument("--legacy", action="store_true")
    ap.add_argument("--min-vm", type=int, default=0)
    ap.add_argument("--negative-controls", action="store_true")
    ARGS = ap.parse_args()
    ARGS.nift = os.path.abspath(ARGS.nift)
    if ARGS.negative_controls:
        return negative_controls()
    its = items()
    if ARGS.sample and ARGS.sample < len(its):
        random.Random(ARGS.seed).shuffle(its)
        its = its[:ARGS.sample]
    bad = ran = diff = 0
    out = open(ARGS.out, "w") if ARGS.out else None
    with Pool(2) as p:
        for res in p.imap_unordered(job, its, chunksize=8):
            ran += 0 if res["refused"] else 1
            diff += 1 if res.get("diff") else 0
            if res["bad"]:
                bad += 1
                if bad <= 20: print("FAIL %s/%s/%s/%s/%s: %s %r" % (res["t"], res["form"], res["v"], res["ctx"],
                                                                  res["obs"], res["bad"], res["vm"][1][-200:]))
            if out: out.write(json.dumps(res) + "\n")
    print("gen_alias: %d programs, %d run by the VM, %d bad%s" % (len(its), ran, bad,
                                                                  ", %d differ from legacy" % diff if ARGS.legacy else ""))
    if ran < ARGS.min_vm:
        print("FAIL gen_alias: only %d programs ran on the VM (at least %d required)" % (ran, ARGS.min_vm))
        return 1
    return 1 if bad else 0


def negative_controls():
    """Two stub binaries: an internal failure (exit 70) and a refusal of every program; each run
    of this generator on a stub (a 200-program sample, --min-vm 1) must fail."""
    ok = True
    scratch = tempfile.mkdtemp(prefix="gen-alias-neg-")
    try:
        stubs = {"exit 70": "echo 'error: NIFT_VM internal failure' >&2; exit 70",
                 "refuses everything": "echo 'error: t.f:1:1: not compiled for the VM (stub)' >&2; exit 1"}
        for label, body in stubs.items():
            stub = os.path.join(scratch, label.replace(" ", "-"))
            open(stub, "w").write("#!/bin/sh\n" + body + "\n")
            os.chmod(stub, 0o755)
            p = subprocess.run([sys.executable, "-I", os.path.abspath(__file__), "--nift", stub, "--sample", "200",
                                "--min-vm", "1"], capture_output=True, text=True,
                               env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
            good = p.returncode == 1
            ok = ok and good
            print("%s negative control gen_alias (%s): exit %d, %s" % ("PASS" if good else "FAIL", label, p.returncode,
                                                                       p.stdout.strip().splitlines()[-1:]))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
