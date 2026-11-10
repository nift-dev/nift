#!/usr/bin/env python3
"""B5 generator gate: gen1 and gen_struct from the candidate tree only (B5 fix-up, review F6).

  gate.py --nift ./nift [--tree DIR] [--which gen1,gen_struct] [--out DIR] [--jobs N]
  gate.py --nift ./nift --negative-controls

Inputs come only from the candidate tree: the generators (this directory) and the tree's builtin
table, src/rt/natives/builtin_list.inc of --tree (default: the tree holding this file). No
implementer or reviewer scratch path is read. Each generated program runs on the VM
(NIFT_VM=require) and, when admitted, on legacy (NIFT_VM=0), in a temporary directory removed
afterwards. Results go to --out (default: a temporary directory, removed unless --out is given).

Fails (exit 1) when any program is `bad`: VM exit 70 or a timeout, a leaked internal tag, internal
text in a diagnostic, or dispatch defects. Prints, per generator, the programs, refusals, admitted
programs and admitted programs whose result differs from legacy (reported, not a failure: an
admitted difference is checked by the semantic gates that own it). Exits 2, writing no program and
no result, when a required input is missing or truncated.

--negative-controls: runs the gate against a missing tree and against a truncated builtin table
and requires exit 2 with "required input missing" / "lists only", no result files and no program
directories, for both generators.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import multiprocessing


def Pool(*args, **kwargs):  # noqa: N802
    # B11 (combined-b3-b5.md section 72): workers get their state through the initializer only,
    # never by inheriting module globals, so every start method works (Python 3.14 defaults to
    # forkserver). NIFT_MP_START_METHOD (fork, forkserver, spawn) forces one for the checks.
    return multiprocessing.get_context(os.environ.get("NIFT_MP_START_METHOD") or None).Pool(*args, **kwargs)

sys.dont_write_bytecode = True  # never leave __pycache__ in the tree
HERE = os.path.dirname(os.path.abspath(__file__))
TREE_DEFAULT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, HERE)

import gen1  # noqa: E402
import gen_struct  # noqa: E402
import genrun  # noqa: E402

GENS = {"gen1": gen1, "gen_struct": gen_struct}


def _init(name, tree, nift, work):
    genrun.set_work(work)
    GENS[name].load(tree, nift)


def run_gate(args):
    which = [w for w in args.which.split(",") if w]
    for w in which:
        if w not in GENS:
            sys.stderr.write("FAIL gate: unknown generator %s\n" % w)
            return 2
    # Every input is checked before anything is written.
    for w in which:
        try:
            GENS[w].load(args.tree, args.nift)
        except gen1.InputMissing as e:
            sys.stderr.write("FAIL %s: %s\n" % (w, e))
            return 2
    if not os.path.isfile(args.nift) or not os.access(args.nift, os.X_OK):
        sys.stderr.write("FAIL gate: required input missing: nift binary %s\n" % args.nift)
        return 2
    keep = args.out is not None
    out = args.out or tempfile.mkdtemp(prefix="b5-gen-")
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="b5-gen-work-")
    failed = False
    try:
        for w in which:
            mod = GENS[w]
            its = mod.items()
            n = bad = refused = admitted = differs = 0
            first_bad = None
            path = os.path.join(out, w + ".jsonl")
            with Pool(args.jobs, initializer=_init, initargs=(w, args.tree, args.nift, work)) as pool, \
                    open(path, "w") as f:
                for res in pool.imap_unordered(mod.job, its, chunksize=8):
                    f.write(json.dumps(res) + "\n")
                    n += 1
                    if res.get("bad"):
                        bad += 1
                        first_bad = first_bad or res
                    if res["refused"]:
                        refused += 1
                    else:
                        admitted += 1
                        if res.get("diff"):
                            differs += 1
            status = "PASS" if bad == 0 and n == len(its) else "FAIL"
            failed = failed or status == "FAIL"
            print("%s %s: %d programs (%d refused, %d admitted, %d admitted differing from legacy), %d bad"
                  % (status, w, n, refused, admitted, differs, bad), flush=True)
            if first_bad:
                print("  first bad: %s %s" % (first_bad["bad"], json.dumps(first_bad["src"])[:300]))
    finally:
        shutil.rmtree(work, ignore_errors=True)
        if not keep:
            shutil.rmtree(out, ignore_errors=True)
    return 1 if failed else 0


def negative_controls(args):
    """Missing and truncated inputs must fail loudly and write nothing."""
    ok = True
    scratch = tempfile.mkdtemp(prefix="b5-gen-neg-")
    try:
        trunc = os.path.join(scratch, "truncated")
        os.makedirs(os.path.join(trunc, "src", "rt", "natives"))
        src = os.path.join(TREE_DEFAULT if args.tree is None else args.tree, "src", "rt", "natives", "builtin_list.inc")
        lines = open(src).read().splitlines()[:20]
        open(os.path.join(trunc, "src", "rt", "natives", "builtin_list.inc"), "w").write("\n".join(lines) + "\n")
        cases = [("missing tree", os.path.join(scratch, "nonexistent", "tree"), "required input missing"),
                 ("truncated builtin table", trunc, "lists only")]
        for w in GENS:
            for label, tree, text in cases:
                out = os.path.join(scratch, "out-%s-%s" % (w, label.replace(" ", "-")))
                tmp = os.path.join(scratch, "tmp-%s-%s" % (w, label.replace(" ", "-")))
                os.makedirs(tmp)
                env = dict(os.environ, TMPDIR=tmp, PYTHONDONTWRITEBYTECODE="1")
                p = subprocess.run([sys.executable, "-I", os.path.abspath(__file__), "--nift", args.nift,
                                    "--tree", tree, "--which", w, "--out", out],
                                   capture_output=True, text=True, env=env)
                wrote = os.path.exists(out) or bool(os.listdir(tmp))
                good = p.returncode == 2 and text in p.stderr and not p.stdout.strip() and not wrote
                ok = ok and good
                print("%s negative control %s (%s): exit %d, %s, results/programs written: %s"
                      % ("PASS" if good else "FAIL", w, label, p.returncode, p.stderr.strip()[:160], wrote))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--tree", default=None)
    ap.add_argument("--which", default="gen1,gen_struct")
    ap.add_argument("--out", default=None)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--negative-controls", action="store_true")
    args = ap.parse_args()
    args.nift = os.path.abspath(args.nift)
    if args.negative_controls:
        return negative_controls(args)
    if args.tree is None:
        args.tree = TREE_DEFAULT
    return run_gate(args)


if __name__ == "__main__":
    sys.exit(main())
