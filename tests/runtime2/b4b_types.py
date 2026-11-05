#!/usr/bin/env python3
"""B4b per-site element and member types (docs/runtime-2/checkpoints/b4b-element-types.md).

  b4b_types.py --nift ./nift [--quick]

  b4b/scripts/*.f    a store through one path (alias, inner alias, callee, closure,
                     global, reordering mutator, dynamic receiver or key, thread or
                     channel transfer, struct field, map contents), observed through
                     another holder with a method, field, presentation or
                     interpolation: refused before any effect, or equal to legacy
  b4b/codegen/*.f    P1 (pre-existing codegen): an assignment used as an expression (constant
                     and register values, nested, in functions and loops): run by the VM,
                     equal to legacy (never a crash)
  b4b/modules/*/     C6: importer stores into a module's container (one importer, two
                     importers, a re-export); `expect` names a refusal reason
  b4b/sessions/*.in  C5: a session function or lambda reading an element stored by a
                     later statement: each statement's output equals legacy's until
                     the VM refuses one (then nothing more runs)
  mutators           C1: every mutating entry point of rt/mutation.h, every
                     CollectionMethod and every native is classified in
                     rt/infer_mutators.h (a new mutator fails until it is)
  generator          wrong-kind swaps: holder path x stored kind x original kind x
                     observing operation: never exit 70, never an internal tag,
                     dispatch_defects 0, refused before any effect or equal to legacy
Equality with legacy allows the approved D-009#5 difference (legacy prefixes
"return: " to an error raised in a returned expression, at another column).
"""
import argparse
import itertools
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "b4b")
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FAILURES = []
CHECKS = [0]
GUARDOFF = [False]  # --guardoff: the reference-probe build (shared references): internal failures only
TAG = "\x1fnift:"


def check(cond, message):
    CHECKS[0] += 1
    if not cond:
        FAILURES.append(message)
        if len(FAILURES) <= 60:
            print("FAIL " + message)


def env_for(d, mode, stats):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C", "LC_ALL": "C",
           "TZ": "UTC", "NIFT_VM": mode, "NO_COLOR": "1"}
    if stats:
        env["NIFT_VM_STATS"] = stats
    return env


def rows_of(path):
    rows = []
    if os.path.exists(path):
        for line in open(path).read().splitlines():
            try:
                rows.append(json.loads(line))
            except ValueError:
                pass
    return rows


def run(nift, files, mode, work, main="main.f", stdin=None):
    d = tempfile.mkdtemp(dir=work)
    for name, text in files.items():
        open(os.path.join(d, name), "w").write(text)
    stats = os.path.join(d, "stats.jsonl")
    cmd = ["timeout", "60", nift] + (["-i"] if stdin is not None else [main])
    p = subprocess.run(cmd, cwd=d, env=env_for(d, mode, stats), input=(stdin or "").encode(),
                       capture_output=True)
    norm = lambda b: b.decode("utf-8", "replace").replace(d + os.sep, "")
    rows = rows_of(stats)
    shutil.rmtree(d, ignore_errors=True)
    return norm(p.stdout), norm(p.stderr), p.returncode, rows


def d009_5(err):
    return re.sub(r"(:\d+):\d+: (?:return: )?", r"\1:C: ", err)


def equal(vm, legacy):
    (vo, ve, vr), (lo, le, lr) = vm, legacy
    return (vo, vr) == (lo, lr) and (ve == le or d009_5(ve) == d009_5(le))


def defects(rows):
    return sum(int(r.get("dispatch_defects", 0)) for r in rows)


def refused(rows, err):
    return any(r.get("outcome") == "require-error" for r in rows) or "not compiled for the VM" in err


def judge(label, nift, files, work, main="main.f"):
    vo, ve, vr, rows = run(nift, files, "require", work, main)
    check(vr != 70 and "internal failure" not in ve and "compiler defect" not in ve,
          "%s: internal failure (exit %d): %r" % (label, vr, ve[-300:]))
    check(TAG not in vo + ve, "%s: an internal tag in the output" % label)
    check(defects(rows) == 0, "%s: dispatch_defects %d" % (label, defects(rows)))
    if refused(rows, ve):
        check(vo == "" and vr == 1 and "no part of it ran" in ve, "%s: refused after an effect: %r/%r" % (label, vo, ve))
        return "refused"
    if GUARDOFF[0]:  # sharing changes meaning against legacy's copies: no equality, internal failures only
        return "vm"
    lo, le, lr, _ = run(nift, files, "0", work, main)
    if TAG in lo + le or "\\u001fnift:" in lo + le:
        return "vm"  # legacy shows an internal tag: the approved B0/CP17/B4 presentation difference
    check(equal((vo, ve, vr), (lo, le, lr)), "%s: VM %r/%r/%d vs legacy %r/%r/%d" % (label, vo, ve, vr, lo, le, lr))
    return "vm"


def tree_files(tree):
    return {name: open(os.path.join(tree, name)).read() for name in os.listdir(tree) if name.endswith(".f")}


def sessions(nift, work):
    dirs = ["sessions"] + (["guardoff"] if GUARDOFF[0] else [])
    expected = {}
    for line in open(os.path.join(HERE, "guardoff", "expected_u6.txt")):
        if line.strip() and not line.startswith("#"):
            name, text = line.split(" ", 1)
            expected[name] = text.strip()
    for sub in dirs:
      for f in sorted(x for x in os.listdir(os.path.join(HERE, sub)) if x.endswith(".in")):
        stdin = open(os.path.join(HERE, sub, f)).read()
        vo, ve, vr, rows = run(nift, {}, "require", work, stdin=stdin)
        label = "session " + f
        internal = vr == 70 or "compiler defect" in ve or "internal failure" in ve
        if GUARDOFF[0] and f in expected:  # the recorded U6 expectation (B5 removes it)
            check(expected[f] in ve, "%s: the recorded U6 expectation no longer shows (%r): update the list" % (label, ve))
            continue
        check(not internal and TAG not in vo + ve and defects(rows) == 0, "%s: %r/%r/%d" % (label, vo, ve, vr))
        if GUARDOFF[0]:
            continue
        lo, le, lr, _ = run(nift, {}, "0", work, stdin=stdin)
        if "NIFT_VM session refused" in ve:
            # Statements before the refused one ran on the VM: legacy's output starts the same.
            check(lo.startswith(vo), "%s: output before the refusal %r is not legacy's %r" % (label, vo, lo))
        else:
            check(vo == lo and d009_5(ve).replace("error: return: ", "error: ") == d009_5(le).replace(
                "error: return: ", "error: "), "%s: VM %r/%r vs legacy %r/%r" % (label, vo, ve, lo, le))


def mutators():
    """C1: the inference's mutator list is derived from the protocol and the method tables."""
    header = open(os.path.join(ROOT, "src", "rt", "mutation.h")).read()
    entries = set(re.findall(r"^(?:inline|NIFT_MUTATION_INLINE) Outcome (\w+)\(", header, re.M))
    entries |= {"collection_sort_init", "collection_plain_sort"}
    table = open(os.path.join(ROOT, "src", "rt", "infer_mutators.h")).read()
    classified = set(re.findall(r'\{"(\w+)", "', table))
    check(len(entries) >= 16, "mutators: found only %d protocol entry points in rt/mutation.h" % len(entries))
    for entry in sorted(entries - classified):
        check(False, "mutators: protocol entry point %s has no classification in rt/infer_mutators.h" % entry)
    methods = re.search(r"enum class CollectionMethod[^{]*\{([^}]*)\}", open(os.path.join(ROOT, "src", "rt",
                                                                                         "collection.h")).read())
    names = [m.strip() for m in methods.group(1).replace("\n", " ").split(",") if m.strip()]
    names = [n for n in names if n != "CountMethods"]
    effects = re.search(r"kCollectionEffects\[\] = \{(.*?)\};", table, re.S).group(1)
    effects = re.sub(r"#if defined\(NIFT_B4B_MUTANT\).*?#else", "", effects, flags=re.S)  # the mutant's line
    commented = re.findall(r"//\s*([A-Za-z ]+?)\s*(?:\(|$)", effects, re.M)
    listed = [w for c in commented for w in c.split()]
    check(listed == names, "mutators: kCollectionEffects comments %r do not list CollectionMethod %r" % (listed, names))
    natives = os.path.join(ROOT, "src", "rt", "natives")
    for f in sorted(os.listdir(natives)):
        if f.endswith(".cpp"):
            check("mutation::" not in open(os.path.join(natives, f)).read(),
                  "mutators: native %s calls the mutation protocol (classify it in rt/infer_mutators.h)" % f)
    known = {"cell.cpp", "collection_plain_sort.cpp", "collection_sort.cpp", "compiler_collection.cpp",
             "vm_array_mutation.cpp", "vm_collection_sort.cpp", "vm_keyed.cpp", "vm_ops.cpp", "vm_report.cpp",
             "vm_struct.cpp", "compiler_invocation.cpp", "session_invocation.cpp", "mutation.cpp",
             "mutation_collection.cpp"}
    rt = os.path.join(ROOT, "src", "rt")
    for f in sorted(os.listdir(rt)):
        if f.endswith(".cpp") and "mutation::" in open(os.path.join(rt, f)).read():
            check(f in known, "mutators: %s uses the mutation protocol and is not in the classified set" % f)


STRUCT = "struct(C) {\n  a := 1\n  fn(get(k)) { return a }\n}\n"
ORIGINAL = {"struct": "C()", "string": '" a "', "map": "map()", "array": "[1]"}
STORED = {"map": "map()", "struct": "C()", "int": "5", "string": '" s "', "array": "[2]",
          "atomic": "atomic<int>(1)", "mutex": "mutex()"}
OBSERVE = ['print(V.trim())', 'print(V.get("k"))', 'print(V.a)', 'print(V.stringify())', 'print(V.size())',
           'print(V.length())', 'print("x$[V]")']
HOLDERS = {
    "alias": ("xs := [O]\nys := xs\nys[0] = S\n", "xs[0]"),
    "alias-back": ("xs := [O]\nys := xs\nxs[0] = S\n", "ys[0]"),
    "inner": ("xs := [[O]]\ninner := xs[0]\ninner[0] = S\n", "xs[0][0]"),
    "member": ('o := {"k": O}\np := o\np["k"] = S\n', "o.k"),
    "callee": ("fn(f(a)) {\n  a[0] = S\n  return 0\n}\nxs := [O]\nf(xs)\n", "xs[0]"),
    "closure": ("xs := [O]\ng := () => {\n  xs[0] = S\n  return 0\n}\ng()\n", "xs[0]"),
    "global": ("xs := [O]\nfn(f()) {\n  xs[0] = S\n  return 0\n}\nf()\n", "xs[0]"),
    "push-reverse": ("xs := [O]\nys := xs\nys.push(S)\nys.reverse()\n", "xs[0]"),
}


def generator(nift, work, quick):
    stats = {"programs": 0, "vm": 0}
    for (h, (store, read)), (ok, o), (sk, s), obs in itertools.product(HOLDERS.items(), ORIGINAL.items(),
                                                                       STORED.items(), OBSERVE):
        if quick and (sk not in ("map", "int", "struct") or ok not in ("struct", "string")):
            continue
        src = STRUCT + store.replace("O", o).replace("S", s) + 'print("before")\n' + obs.replace("V", read) + "\n"
        stats["programs"] += 1
        if judge("generator %s %s->%s %s" % (h, ok, sk, obs), nift, {"main.f": src}, work) == "vm":
            stats["vm"] += 1
    return stats


def containers(n):
    """n containers (an array, an object inside it, an array inside that: three sites each), read
    through a parameter, plus a fresh array per index pushed to and stored into."""
    lines = []
    for i in range(n):
        lines += ['a%d := [%d, "s", {"k": [%d]}]' % (i, i, i), "c%d := [%d]" % (i, i), "c%d.push(%d)" % (i, i),
                  "c%d[0] = %d" % (i, i + 1), "fn(h%d(x)) {" % i, "  return x[0] + x[2].k[0]", "}",
                  "print(h%d(a%d) + c%d[1] + a%d[1].length())" % (i, i, i, i)]
    return "\n".join(lines) + "\n"


def glob(n):
    """Review H2: n helpers each pushing a new container (its own site) into one global array."""
    kinds = ["%d" % 1, '"t"', "2.5", "true", "null"]
    lines = ['acc := [[" s "]]']
    for i in range(n):
        lines += ["fn(k%d()) {" % i, '  acc.push([%s, " s "])' % kinds[i % len(kinds)], "  return acc.size()", "}"]
    lines += ["print(k%d())" % i for i in range(n)] + ["print(acc[1][1].trim())"]
    return "\n".join(lines) + "\n"


def best_compile(nift, src, work, runs=7):
    best = None
    for _ in range(runs):
        _, _, _, rows = run(nift, {"main.f": src}, "require", work)
        us = rows[-1].get("compile_us") if rows and rows[-1].get("outcome") == "vm" else None
        best = us if best is None or (us is not None and us < best) else best
    return best


def compile_scaling(nift, work, baseline=None):
    """Compile time: the container-heavy shape (four times the containers within twelve times the
    time) and the review H2 shape (one global array, n helpers each adding a site: near-linear,
    four times the helpers within six times the time; with --baseline, within 1.5x of B4a at
    every size)."""
    shapes = (("containers", containers, (250, 1000), 12), ("glob", glob, (100, 400, 800, 1600), 6))
    growth = {}
    for name, make, sizes, bound in shapes:
        times = {n: best_compile(nift, make(n), work) for n in sizes}
        for small, large in zip(sizes, sizes[1:]):
            check(times[small] is not None and times[large] is not None and times[large] <= bound * times[small],
                  "compile scaling %s: %r us for %d, %r us for %d (must be <= %dx)" %
                  (name, times[small], small, times[large], large, bound))
        line = "compile scaling %s: %s" % (name, ", ".join("%d %.0f us" % (n, times[n] or 0) for n in sizes))
        if baseline and name == "glob":
            # Review H2: the growth is gone, not moved to larger sizes: the ratio to B4a does not
            # rise from size to size by more than noise (10%), besides staying within 1.5x.
            base_times = {n: best_compile(baseline, make(n), work) for n in sizes}
            ratios = [(times[n] or 0) / (base_times[n] or 1) for n in sizes]
            for (small, r1), (large, r2) in zip(zip(sizes, ratios), zip(sizes[1:], ratios[1:])):
                check(r2 <= r1 * 1.10 + 0.05, "compile scaling glob: the ratio to B4a grows from %.2fx at %d to %.2fx at %d"
                      % (r1, small, r2, large))
        if baseline:
            base = {n: best_compile(baseline, make(n), work) for n in sizes}
            for n in sizes:
                check(times[n] is not None and base[n] and times[n] <= 1.5 * base[n],
                      "compile scaling %s %d: %r us against B4a %r us (must be <= 1.5x)" % (name, n, times[n], base[n]))
            line += "; B4a " + ", ".join("%d %.0f us (%.2fx)" % (n, base[n] or 0, (times[n] or 0) / (base[n] or 1))
                                         for n in sizes)
        print(line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--guardoff", action="store_true")
    ap.add_argument("--baseline", help="B4a nift: the compile scaling shapes within 1.5x of it")
    args = ap.parse_args()
    GUARDOFF[0] = args.guardoff
    nift = os.path.abspath(args.nift)
    work = tempfile.mkdtemp(prefix="b4b-types-")
    try:
        mutators()
        outcomes = {"refused": 0, "vm": 0}
        for f in sorted(os.listdir(os.path.join(HERE, "scripts"))):
            outcomes[judge("script " + f, nift, {"main.f": open(os.path.join(HERE, "scripts", f)).read()}, work)] += 1
        # P1 (pre-existing codegen): assignment-as-expression fixtures run on the VM and equal legacy.
        for f in sorted(os.listdir(os.path.join(HERE, "codegen"))):
            got = judge("codegen " + f, nift, {"main.f": open(os.path.join(HERE, "codegen", f)).read()}, work)
            check(got == ("refused" if f.startswith("refused_") else "vm"), "codegen %s: %s" % (f, got))
        for case in sorted(os.listdir(os.path.join(HERE, "modules"))):
            tree = os.path.join(HERE, "modules", case)
            expect = open(os.path.join(tree, "expect")).read().split() if os.path.exists(
                os.path.join(tree, "expect")) else []
            vo, ve, vr, rows = run(nift, tree_files(tree), "require", work)
            if expect and expect[0] == "refuse":
                check(vr == 1 and vo == "" and "(%s)" % expect[1] in ve, "module %s: expected the %s refusal, got %r/%r"
                      % (case, expect[1], vo, ve[-300:]))
            else:
                outcomes[judge("module " + case, nift, tree_files(tree), work)] += 1
        sessions(nift, work)
        print("fixtures: %(vm)d run by the VM (equal to legacy), %(refused)d refused before any effect" % outcomes)
        if not args.guardoff:
            compile_scaling(nift, work, args.baseline)
        stats = generator(nift, work, args.quick)
        print("generator: %(programs)d programs, %(vm)d run by the VM" % stats)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if FAILURES:
        print("FAIL b4b-types: %d checks, %d failure(s)" % (CHECKS[0], len(FAILURES)))
        return 1
    print("PASS b4b-types: %d checks, 0 failure(s)" % CHECKS[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
