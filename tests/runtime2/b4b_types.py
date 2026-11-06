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
  b4b/widen/*.f      B4b 22 (review of 16f24f36): a precise class merged into a class opened
                     (widened) earlier, directly and transitively (A -> B -> C), in both merge
                     orders, in functions, lambdas and globals: refused before any effect,
                     `aliasing` by the guard, `builtin-receiver:trim` with --guardoff (the
                     reader's `trim` is on anything); never exit 70
  b4b/admission/     B4b 23: the recorded admission differences against B4a and a42035c1
                     (expect.txt; `glob` from 16 helpers refused `builtin-receiver:trim`
                     before any effect, exit 1), each with its expected-future marker
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


def many(n):
    """F1: n helpers each pushing a new array (its own site) into the one array passed to all of them."""
    kinds = ["%d" % 1, '"t"', "2.5", "true", "null"]
    lines = []
    for i in range(n):
        lines += ["fn(k%d(r)) {" % i, '  r.push([%s, " s "])' % kinds[i % len(kinds)], "  return r.size()", "}"]
    lines += ['acc := [[" s "]]'] + ["print(k%d(acc))" % i for i in range(n)] + ["print(acc[0][1].trim())"]
    return "\n".join(lines) + "\n"


def nestb(n):
    """F1: the same with the pushed array passed in too (a parameter's sites stored into a parameter's)."""
    values = ["[1]", '["t"]', "[[1]]", '[{"q": 1}]', "[null]", "[true]", "[2.5]"]
    lines = []
    for i in range(n):
        lines += ["fn(k%d(r, v)) {" % i, "  r.push(v)", "  return r.size()", "}"]
    lines += ['acc := [[" s "]]']
    for i in range(n):
        lines += ["t%d := %s" % (i, values[i % len(values)]), "print(k%d(acc, t%d))" % (i, i)]
    return "\n".join(lines + ["print(acc.size())"]) + "\n"


def globp(n):
    """F1: `glob` observed by print (admitted while the guard is on): the VM's output equals legacy's."""
    return glob(n).replace("print(acc[1][1].trim())", "print(acc[1][1])")


def widen(nift, work):
    """B4b 22: the readers of a class that a union opens read anything (none keeps a Static `trim`):
    each fixture is refused before any effect, by the guard (`aliasing`) or, guard off, by the
    admission of `trim` on anything; the guard-off run never reaches the VM (exit 70 before)."""
    expected = "builtin-receiver:trim" if GUARDOFF[0] else "aliasing"
    d = os.path.join(HERE, "widen")
    for f in sorted(x for x in os.listdir(d) if x.endswith(".f")):
        label = "widen " + f
        got = judge(label, nift, {"main.f": open(os.path.join(d, f)).read()}, work)
        _, ve, _, rows = run(nift, {"main.f": open(os.path.join(d, f)).read()}, "require", work)
        reasons = [str(r.get("reason", "")) for r in rows]
        check(got == "refused" and expected in reasons, "%s: expected the %s refusal, got %s %r" %
              (label, expected, got, reasons or ve[-200:]))


def admission(nift, work):
    """B4b 23: each recorded admission difference holds as recorded (the same with --guardoff): a
    refusal is before any effect with exit 1 (judge), and its reason is the recorded one. The
    expected-future marker is printed: a change of outcome must update expect.txt and the record."""
    d = os.path.join(HERE, "admission")
    rows = [line.split(None, 3) for line in open(os.path.join(d, "expect.txt"))
            if line.strip() and not line.startswith("#")]
    for name, now, before, future in rows:
        src = open(os.path.join(d, name)).read()
        label = "admission " + name
        got = judge(label, nift, {"main.f": src}, work)
        _, ve, vr, rows_ = run(nift, {"main.f": src}, "require", work)
        if now.startswith("refuse:"):
            reason = now.split(":", 1)[1]
            check(got == "refused" and vr == 1 and any(r.get("reason") == reason for r in rows_),
                  "%s: expected the %s refusal (exit 1, before any effect), got %s %r" % (label, reason, got, ve[-200:]))
        else:
            check(got == now, "%s: expected %s, got %s %r" % (label, now, got, ve[-200:]))
        if future.startswith("recover:"):
            print("admission %s: %s (B4a and a42035c1: %s); expected future: %s" % (name, now, before, future))


def compile_row(nift, src, work):
    _, _, _, rows = run(nift, {"main.f": src}, "require", work)
    return rows[-1] if rows else {}


def compile_us(nift, src, work):
    """One compile time, whatever the outcome (the guard refuses `many` and `nestb` after the
    analysis); None if the run stopped at an analysis limit (F1)."""
    row = compile_row(nift, src, work)
    if not row or str(row.get("reason", "")).startswith("analysis-limit"):
        return None
    return row.get("compile_us")


def convergence(nift, work):
    """F1: from 15 to 17 helpers (the 16-class bound) and at 100, every shape is refused before any
    effect or equal to legacy, and never by the analysis limit (the widening converges)."""
    for name, make in (("many", many), ("nestb", nestb), ("glob", glob), ("globp", globp)):
        for n in (15, 16, 17, 100):
            src = make(n)
            judge("convergence %s %d" % (name, n), nift, {"main.f": src}, work)
            reason = str(compile_row(nift, src, work).get("reason", ""))
            check(not reason.startswith("analysis-limit"), "convergence %s %d: refused %s" % (name, n, reason))


def median(values):
    values = sorted(values)
    k = len(values) // 2
    return values[k] if len(values) % 2 else (values[k - 1] + values[k]) / 2.0


def compile_scaling(nift, work, baseline=None, runs=7):
    """Compile time (`compile_us`) of the scaling shapes at 100, 400, 800 and 1,600 helpers: the
    container-heavy shape, the review H2 shape (one global array, n helpers each adding a site) and
    the F1 shapes (`many`, `nestb`: the array a parameter). Guidance only (B4b 23): the gate is the
    deterministic analysis-work count (tests/runtime2/b4b/work.py); a wall-clock bound is flaky on a
    shared machine. Printed: the median of `runs` runs, with --baseline (B4a) interleaved run by
    run, and the ratio of the medians (the Linode objective: within 1.5x of B4a). Checked: no run
    stops at an analysis limit (deterministic)."""
    sizes = (100, 400, 800, 1600)
    for name, make in (("containers", containers), ("glob", glob), ("many", many), ("nestb", nestb)):
        cells = []
        for n in sizes:
            src = make(n)
            tip, base = [], []
            for _ in range(runs):  # interleaved, so a load change affects both alike
                t = compile_us(nift, src, work)
                check(t is not None, "compile scaling %s %d: stopped at an analysis limit" % (name, n))
                tip.append(t or 0)
                if baseline:
                    base.append(compile_us(baseline, src, work) or 0)
            cell = "%d %.0f us" % (n, median(tip))
            if baseline:
                cell += " (B4a %.0f us, %.2fx)" % (median(base), median(tip) / (median(base) or 1))
            cells.append(cell)
        print("compile scaling %s (median of %d, guidance): %s" % (name, runs, ", ".join(cells)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--guardoff", action="store_true")
    ap.add_argument("--baseline", help="B4a nift: the compile times printed against it (guidance)")
    ap.add_argument("--runs", type=int, default=7, help="compile scaling: runs per size (the median)")
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
        widen(nift, work)
        admission(nift, work)
        convergence(nift, work)
        print("fixtures: %(vm)d run by the VM (equal to legacy), %(refused)d refused before any effect" % outcomes)
        if not args.guardoff:
            compile_scaling(nift, work, args.baseline, args.runs)
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
