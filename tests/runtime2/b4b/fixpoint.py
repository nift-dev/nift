#!/usr/bin/env python3
"""B4b 27: the fixed-point gate (docs/runtime-2/checkpoints/b4b-element-types.md section 18).

  fixpoint.py --nift FIXPOINT_NIFT --guardoff FIXPOINT_GUARDOFF_NIFT [--quick] [--jobs N]
  fixpoint.py --self-test --nift FIXPOINT_NIFT

The two binaries are the fixed-point builds (Makefile B4B_FIXPOINT_NIFT and
B4B_FIXPOINT_OFF_NIFT: the CLI with the guard and the guard-off CLI, each with the kind
inference's entry compiled with -DNIFT_B4B_FIXPOINT_GATE). In them every compile runs the
production analysis to convergence and then the after-convergence check
(Inference::check_fixed_point): one more walk of every unit must grow no analysis state (no
unit queued again, no walk that grows its own inputs, no new lambda unit) and must change no
per-node fact the compiler reads. A non-fixed point is never repaired: the compile fails
(`B4B-FIXPOINT:` on stderr, exit 86) and this gate fails, listing every one.

Programs, on both builds:
  corpus      every script fixture under tests/runtime2 (guardoff.py's set, in temporary copies),
              run (NIFT_VM=require: the units the run compiles, eval and modules included) and
              classified (NIFT_VM=classify-all: every unit);
  cp25-r      the CP25-R classify-all corpus (tests/runtime2/cp25/inventory.py);
  sessions    every session fixture (*.in) under tests/runtime2 (each statement's compile);
  generators  the b4b_types.py wrong-kind generator, gen_alias.py (all programs), the reviewers'
              wrong-kind generators (gen_wrongkind.py: gen1 pass a, pass b, gen_struct) and the
              compile-scaling shapes (b4b_types.py: glob, many, nestb, containers at 100, 400),
              classified (NIFT_VM=classify-all).
--quick: a deterministic sample of each generator (1 in 8) and no classify-all corpus pass.
The gate also fails on a compile that crashed or timed out on these builds (exit 70 is not
judged here: the guard-off gate and the b4b suite judge outcomes)."""
import argparse
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from multiprocessing import Pool

HERE = os.path.dirname(os.path.abspath(__file__))
RT2 = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(RT2))
sys.path.insert(0, RT2)
sys.path.insert(0, HERE)
import b4b_types  # noqa: E402
import gen_alias  # noqa: E402
import gen_wrongkind  # noqa: E402
import guardoff  # noqa: E402

MARK = "B4B-FIXPOINT:"
STATE = {}


KEEP = [None]  # --keep DIR: a copy of each failing program's directory (triage)


def launcher(binary, log, work, name):
    """An executable the harnesses call in place of nift (they clear the environment): it runs
    `binary` and records each failed check (the `B4B-FIXPOINT:` line, or exit 86) in `log`."""
    path = os.path.join(work, name)
    with open(path, "w") as f:
        f.write("""#!/bin/sh
err=$(mktemp "%(work)s/err.XXXXXX")
"%(binary)s" "$@" 2>"$err"
rc=$?
if [ $rc -eq 86 ] || grep -q '%(mark)s' "$err"; then
  { printf '%%s exit %%d: ' "$(pwd) $*" $rc; grep '%(mark)s' "$err" || echo; } >> "%(log)s"
  %(keep)s
fi
cat "$err" >&2
rm -f "$err"
exit $rc
""" % {"work": work, "binary": binary, "mark": MARK, "log": log, "keep": keep_command()})
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return path


def keep_command():
    if not KEEP[0]:
        return ":"
    return 'k="%s/$(date +%%s%%N)"; mkdir -p "$k"; cp -r . "$k/" 2>/dev/null; echo "$*" > "$k/.args"' % KEEP[0]


def compile_source(job):
    """Classify one generated program (every unit compiled, nothing run)."""
    nift, src, mode = job
    d = tempfile.mkdtemp(dir=STATE["work"])
    try:
        open(os.path.join(d, "t.f"), "w").write(src)
        env = {"PATH": "/usr/bin:/bin", "HOME": d, "TMPDIR": d, "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
               "NO_COLOR": "1", "NIFT_VM": mode}
        p = subprocess.run(["timeout", "60", nift, "t.f"], cwd=d, env=env, stdin=subprocess.DEVNULL,
                           capture_output=True)
        return p.returncode
    finally:
        shutil.rmtree(d, ignore_errors=True)


def init(work):
    STATE["work"] = work


def crashed(rc):
    return rc >= 124 or rc < 0


def run_sources(label, nift, sources, jobs, work, mode="classify-all"):
    bad = 0
    with Pool(jobs, initializer=init, initargs=(work,)) as pool:
        for rc in pool.imap_unordered(compile_source, [(nift, s, mode) for s in sources], chunksize=16):
            bad += crashed(rc) and rc != 86
    print("  %s: %d programs (%d crashed or timed out)" % (label, len(sources), bad), flush=True)
    return bad


def corpus_files(dirs=None):
    """guardoff.py's script set (every .f under tests/runtime2 but module directories)."""
    files = []
    for top in [os.path.join(RT2, d) for d in dirs] if dirs else [RT2]:
        for dp, dn, fn in os.walk(top):
            dn.sort()
            if "__pycache__" in dp:
                continue
            files += [os.path.join(dp, f) for f in sorted(fn) if f.endswith(".f") and not dp.endswith("/modules")]
    return files


def corpus_job(job):
    nift, path, mode = job
    guardoff.WORK[0] = tempfile.mkdtemp(dir=STATE["work"])
    try:
        return guardoff.run(nift, path, mode)[2]
    finally:
        shutil.rmtree(guardoff.WORK[0], ignore_errors=True)


def run_corpus(label, nift, modes, jobs, work, dirs=None):
    files = corpus_files(dirs)
    bad = 0
    for mode in modes:
        with Pool(jobs, initializer=init, initargs=(work,)) as pool:
            for rc in pool.imap_unordered(corpus_job, [(nift, f, mode) for f in files], chunksize=8):
                bad += crashed(rc) and rc != 124 and rc != 86
        print("  %s corpus (%s): %d scripts" % (label, mode, len(files)), flush=True)
    return bad


def run_sessions(label, nift, work):
    n = 0
    for dp, dn, fn in os.walk(RT2):
        dn.sort()
        for f in sorted(x for x in fn if x.endswith(".in")):
            stdin = open(os.path.join(dp, f)).read()
            b4b_types.run(nift, {}, "require", work, stdin=stdin)
            n += 1
    print("  %s sessions: %d" % (label, n), flush=True)


def run_cp25(label, nift, work):
    sys.path.insert(0, os.path.join(RT2, "cp25"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "rt-vm"))
    import inventory  # noqa: E402
    out = tempfile.mkdtemp(dir=work)
    n, kept = inventory.collect_classify_all(nift, out, 1)
    print("  %s CP25-R classify-all: %d units (%d kept)" % (label, n, kept), flush=True)


def generated(quick):
    pick = (lambda xs: xs[::8]) if quick else (lambda xs: xs)
    sets = [
        ("b4b_types generator", [s for _, s in b4b_types.generator_sources(False)]),
        ("gen_alias", [it["src"] for it in gen_alias.items()]),
        ("gen1 pass a", gen_wrongkind.gen1("a")),
        ("gen1 pass b", gen_wrongkind.gen1("b")),
        ("gen_struct", gen_wrongkind.gen_struct()),
        ("scaling shapes", [make(n) for make in (b4b_types.glob, b4b_types.many, b4b_types.nestb,
                                                 b4b_types.containers) for n in (100, 400)]),
    ]
    return [(label, pick(srcs) if label != "scaling shapes" else srcs) for label, srcs in sets]


def gate(args):
    work = tempfile.mkdtemp(prefix="b4b-fixpoint-")
    log = os.path.join(work, "fixpoint.log")
    open(log, "w").close()
    bad = 0
    try:
        if args.corpus_only:  # the mutant suite: the given corpus directories, guard on, run only
            bad += run_corpus("guard-on", launcher(os.path.abspath(args.nift), log, work, "nift-guard-on"),
                              ["require"], args.jobs, work, args.corpus_only)
        for label, binary in () if args.corpus_only else (("guard-on", args.nift), ("guard-off", args.guardoff)):
            nift = launcher(os.path.abspath(binary), log, work, "nift-" + label)
            print("fixpoint %s (%s):" % (label, binary), flush=True)
            bad += run_corpus(label, nift, ["require"] if args.quick else ["require", "classify-all"], args.jobs, work)
            run_cp25(label, nift, work)
            run_sessions(label, nift, work)
            for name, sources in generated(args.quick):
                bad += run_sources(name, nift, sources, args.jobs, work)
        entries = [line.rstrip("\n") for line in open(log) if line.strip()]
        if args.log:
            shutil.copy(log, args.log)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for e in entries[:60]:
        print("FAIL non-fixed point: " + e)
    ok = not entries and not bad
    print("%s b4b fixed-point gate: %d non-fixed point(s), %d crash(es)" % ("PASS" if ok else "FAIL", len(entries), bad))
    return 0 if ok else 1


def self_test(args):
    """The gate's plumbing: a binary reporting a non-fixed point (and exiting 86) is recorded through
    the wrapper by each harness used (a generated program, a corpus script, a session), and a
    fixed-point build's ordinary run is not."""
    work = tempfile.mkdtemp(prefix="b4b-fixpoint-self-")
    try:
        fake = os.path.join(work, "fake-nift")
        open(fake, "w").write("#!/bin/sh\necho '%s not a fixed point (1 node(s)): 1:1[x]' >&2\nexit 86\n" % MARK)
        os.chmod(fake, 0o755)
        results = []
        for binary, expect in ((fake, True), (os.path.abspath(args.nift), False)):
            log = os.path.join(work, "log")
            open(log, "w").close()
            nift = launcher(binary, log, work, "w")
            init(work)
            compile_source((nift, 'print("x")\n', "classify-all"))
            corpus_job((nift, os.path.join(HERE, "scripts", sorted(os.listdir(os.path.join(HERE, "scripts")))[0]),
                        "require"))
            b4b_types.run(nift, {}, "require", work, stdin='print("x")\n')
            got = sum(1 for line in open(log) if line.strip())
            results.append((binary, expect, got))
        ok = results[0][2] == 3 and results[1][2] == 0
        for binary, expect, got in results:
            print("  %s: %d recorded (expected %s)" % (os.path.basename(binary), got, "3" if expect else "0"))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("%s b4b fixed-point gate self-test" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--guardoff")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--corpus-only", nargs="+", help="only these tests/runtime2 directories, guard on (mutants)")
    ap.add_argument("--keep", help="copy each failing program's directory here (triage)")
    ap.add_argument("--log", help="also write every non-fixed point to this file")
    args = ap.parse_args()
    KEEP[0] = os.path.abspath(args.keep) if args.keep else None
    if args.self_test:
        return self_test(args)
    if not args.guardoff and not args.corpus_only:
        ap.error("--guardoff is required")
    return gate(args)


if __name__ == "__main__":
    sys.exit(main())
