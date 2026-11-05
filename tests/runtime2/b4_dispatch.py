#!/usr/bin/env python3
"""B4 operation admission and dynamic dispatch (docs/runtime-2/checkpoints/b4-dispatch.md).

  b4_dispatch.py --nift ./nift [--update] [--quick]

  b4/dispatch/*.f   compiled by the VM (NIFT_VM=require) and equal to legacy
                    (NIFT_VM=0): stdout, stderr and exit status; a `.vm` golden
                    marks an approved difference (D-009#5), which must still
                    differ from legacy
  b4/concurrent/*.f run on the VM with legacy's final output, three times (an
                    atomic presented while a thread updates it; TSan lane too)
  b4/c1/*/          C1: per import-collision shape, legacy's error line as a program
                    error before running (`expect` program-error), or the capability
                    refusal (`expect` refuse); a `legacy` file lists output legacy gives
                    instead of the collision (it stops first: review M2)
  b4/refuse/*.f     refused before any effect, with the `.reason` given
  sessions          piped sessions (SESSIONS): equal to legacy
  enumeration       every built-in method name of the plain families x every
                    receiver kind (null, bool, int, double, string, array,
                    object; and map, set, stack, struct, mutex, atomic, a
                    lambda, a channel mixed with a string) x four contexts
                    (loop code, a function, a function called from loop code,
                    top level): never exit 70, and where the VM runs it, equal
                    to legacy for the plain kinds (the APPROVED pre-existing
                    differences aside; the other kinds' legacy forms are tag
                    strings, CP17/B0)
Every VM run must report "dispatch_defects": 0 (NIFT_VM_STATS): no method
native ever met a receiver outside its family at an unflagged site.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "b4")
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FAILURES = []
CHECKS = [0]


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


def stats_lines(path):
    rows = []
    if os.path.exists(path):
        for line in open(path).read().splitlines():
            try:
                rows.append(json.loads(line))
            except ValueError:
                pass
    return rows


def run_text(nift, source, mode, work, name="t.f"):
    d = tempfile.mkdtemp(dir=work)
    open(os.path.join(d, name), "w").write(source)
    stats = os.path.join(d, "stats.jsonl")
    p = subprocess.run(["timeout", "60", nift, name], cwd=d, env=env_for(d, mode, stats), stdin=subprocess.DEVNULL,
                       capture_output=True)
    norm = lambda b: b.decode("utf-8", "replace").replace(d + os.sep, "")
    rows = stats_lines(stats)
    shutil.rmtree(d, ignore_errors=True)
    return norm(p.stdout), norm(p.stderr), p.returncode, rows


def run_session(nift, stdin, mode, work):
    d = tempfile.mkdtemp(dir=work)
    stats = os.path.join(d, "stats.jsonl")
    p = subprocess.run(["timeout", "60", nift, "-i"], cwd=d, env=env_for(d, mode, stats), input=stdin.encode(),
                       capture_output=True)
    rows = stats_lines(stats)
    shutil.rmtree(d, ignore_errors=True)
    return p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"), p.returncode, rows


def run_display(nift, spec, work):
    d = tempfile.mkdtemp(dir=work)
    if isinstance(spec, list):
        p = subprocess.run(["timeout", "60", nift] + spec, cwd=d, env=env_for(d, "require", None),
                           stdin=subprocess.DEVNULL, capture_output=True)
    else:
        p = subprocess.run(["timeout", "60", nift, "-i"], cwd=d, env=env_for(d, "require", None), input=spec.encode(),
                           capture_output=True)
    shutil.rmtree(d, ignore_errors=True)
    return p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"), p.returncode


def run_tree(nift, tree, mode, work):
    d = tempfile.mkdtemp(dir=work)
    for f in os.listdir(tree):
        if f != "expect":
            shutil.copy(os.path.join(tree, f), os.path.join(d, f))
    p = subprocess.run(["timeout", "60", nift, "main.f"], cwd=d, env=env_for(d, mode, None), stdin=subprocess.DEVNULL,
                       capture_output=True)
    norm = lambda b: b.decode("utf-8", "replace").replace(d + os.sep, "")
    shutil.rmtree(d, ignore_errors=True)
    return norm(p.stdout), norm(p.stderr), p.returncode


def defects(rows):
    return sum(int(r.get("dispatch_defects", 0)) for r in rows)


def fixtures(kind):
    d = os.path.join(HERE, kind)
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".f"))


# Non-disclosure (owner): no VM output on any B4 path shows an internal tag.
TAG = "\x1fnift:"

# B4 presentation (b4-dispatch.md 6): sessions and `nift eval` where the VM's
# display is the approved one: (name, stdin or eval argv, stdout, stderr part).
DISPLAYS = [
    ("session_struct", 'struct(C){a:=1;private s:="SECRET-b4";fn(stringify()){print("SIDE-EFFECT");return "u"}}\n'
                       'c := C()\nc\n', "C{a:1}\n", ""),
    ("session_map", 'm := map()\nm.set("k", 1)\nm\n', "1\nmap([[\"k\",1]])\n", ""),
    ("session_atomic", 'a := atomic<int>(5)\na\na.store(7)\na\n', "5\n7\n", ""),
    ("session_function", 'f := (x) => x\nf\nprint("after")\n', "after\n", "opaque runtime value is not serializable"),
    ("eval_function", ["eval", "(x) => x"], "", "opaque runtime value is not serializable"),
    ("eval_function_nested", ["eval", "[(x) => x]"], "", "opaque runtime value is not serializable"),
    ("eval_atomic", ["eval", "atomic<int>(5)"], "5\n", ""),
]
# Review L1: `nift eval --json` (machine-readable) is unchanged by B4: B3's exact
# output and exit status (a function is its tag string, an atomic not serializable).
JSON_DISPLAYS = [
    ("json_atomic", "atomic<int>(5)", "", "eval: opaque runtime value is not serializable\n", 2),
    ("json_atomic_nested", "[atomic<int>(5)]", "", "eval: opaque runtime value is not serializable\n", 2),
    ("json_function", "(x) => x", '"\\u001fnift:callable:lambda:1"\n', "", 0),
    ("json_function_nested", "[(x) => x]", '[\n"\\u001fnift:callable:lambda:1"\n]\n', "", 0),
]

SESSIONS = [
    ("union_loop", 'for (v : [" a ", 7]) { print(v.trim()) }\nprint("after")\n'),
    ("union_loop_natives", 'for (v : ["xa", 2.5, 3]) { print(v.to_string()) }\n'),
    ("union_failure", 'fn(f(v)) { print(v.trim()) }\nf(" x ")\nf(3)\nprint("next statement")\n'),
    ("union_param", 'fn(has(v)) { r := v.contains("a"); return r }\nprint(has("xa"))\nprint(has(["a"]))\n'
                    'print(has(5))\nprint("after")\n'),
]

# ---- the spec x kind enumeration ----
STRARG = {"split", "index_of", "last_index_of", "contains", "starts_with", "ends_with", "join", "has", "get",
          "encode", "indexOf"}
PLAIN_KINDS = {"null": "null", "bool": "true", "int": "5", "frac": "2.5", "string": '" s "', "array": "[3,1]",
               "object": '{"a":1}'}
OTHER_KINDS = {"map": "map()", "set": "set()", "stack": "stack()", "mutex": "mutex()",
               "atomic": "atomic<int>(4)", "lambda": "(x) => x", "channel": "channel(1)"}
CONTEXTS = ("loop", "fn", "fnloop", "top")
# Pre-existing differences of the static path that the dynamic one shares,
# each established on the B3 binary (b4-dispatch.md section 3); not B4's.
APPROVED = {
    ("to_int", "frac", "loop"): "native error column: the static path reports the call, legacy the print argument",
    ("to_int", "frac", "fnloop"): "the same, in a function called from loop code",
}


def method_names():
    names = {}
    for line in open(os.path.join(ROOT, "src", "rt", "natives", "builtin_list.inc")):
        m = re.match(r'NIFT_RT_BUILTIN\((\w+), "(\w+)", (\w+), (\d+), (\w+)', line)
        if m and m.group(3) in ("String", "Array", "Object", "Number", "Bool", "Null", "Any"):
            names.setdefault(m.group(2), int(m.group(4)))
    return names


def call_text(name, argc):
    if name == "replace":
        return 'replace("a", "b")'
    if argc == 0:
        return name + "()"
    if name in ("merge", "merge_deep"):
        return name + '({"z":1})'
    if name in ("pick", "omit"):
        return name + '(["a"])'
    return name + ('("a")' if name in STRARG else "(1)")


def program(ctx, first, second, call, struct=False):
    head = 'struct(C) { a := 1 }\n' if struct else ""
    if ctx == "loop":
        return head + 'print("b")\nfor (v : [%s, %s]) { print(v.%s) }\n' % (first, second, call)
    if ctx == "fn":
        return head + 'print("b")\nfn(f(v)) { print(v.%s) }\nf(%s)\nf(%s)\n' % (call, first, second)
    if ctx == "fnloop":
        return head + 'print("b")\nfn(f(v)) { print(v.%s) }\nfor (i : [1]) { f(%s) }\nf(%s)\n' % (call, first, second)
    return head + 'print("b")\nxs := [%s, %s]\nv := xs[0]\nprint(v.%s)\n' % (first, second, call)


def enumeration(nift, work, quick):
    names = method_names()
    stats = {"programs": 0, "vm": 0, "refused": 0}
    for name, argc in sorted(names.items()):
        call = call_text(name, argc)
        kinds = list(PLAIN_KINDS.items()) + list(OTHER_KINDS.items()) + [("struct", "C()")]
        for kind, value in kinds:
            other = "7" if kind == "string" else '"q"'
            for ctx in (CONTEXTS[:2] if quick else CONTEXTS):
                src = program(ctx, value, other, call, struct=kind == "struct")
                vo, ve, vr, rows = run_text(nift, src, "1", work)
                stats["programs"] += 1
                label = "enumeration %s.%s on %s (%s)" % (name, call, kind, ctx)
                check(TAG not in vo + ve, "%s: an internal tag in the VM's output" % label)
                check(vr != 70 and "internal failure" not in ve and "compiler defect" not in ve,
                      "%s: internal failure (exit %d): %r" % (label, vr, ve[-200:]))
                check(defects(rows) == 0, "%s: dispatch_defects %d" % (label, defects(rows)))
                outcome = rows[-1].get("outcome") if rows else None
                if outcome not in ("vm", "vm-error"):
                    stats["refused"] += 1
                    continue
                stats["vm"] += 1
                if (name, kind, ctx) in APPROVED or kind not in PLAIN_KINDS:
                    continue  # other kinds: CP17/B0 presentation differences; checked for exit 70 and defects
                lo, le, lr, _ = run_text(nift, src, "0", work)
                check((vo, ve, vr) == (lo, le, lr), "%s: VM %r/%r/%d vs legacy %r/%r/%d" %
                      (label, vo[-120:], ve[-160:], vr, lo[-120:], le[-160:], lr))
    return stats


# B0's root cause, structurally (b4-dispatch.md 4): an interpolated part mixing
# one scalar family with kinds that have no text form converts through a
# flagged to_string; those kinds fail with B0's text, from the dispatch.
INTERP_SCALARS = {"int": ("1", "1"), "frac": ("2.5", "2.5"), "string": ('"s"', "s"), "bool": ("true", "true"),
                  "null": ("null", "null")}
INTERP_OTHERS = {
    "map": ("map()", "map, set and stack values cannot be rendered as text"),
    "stack": ("stack()", "map, set and stack values cannot be rendered as text"),
    "mutex": ("mutex()", "opaque runtime value is not serializable"),
    "lambda": ("(x) => x", "opaque runtime value is not serializable"),
}


# Review H1: a proven struct receiver x every method name x seven contexts x three
# arities (the reviewer's generator): wherever the VM runs it, equal to legacy,
# except D-009#5 (legacy prefixes "return: " to an error raised in a returned
# expression and reports the return's column).
STRUCT_EXTRA = ("nosuch", "push", "pop", "sort", "map", "filter", "get", "set", "insert", "remove", "reverse", "keys")
STRUCT_CONTEXTS = {
    "top": 'c := S()\nprint("before")\nprint(c.CALL)\nprint("after")\n',
    "for": 'c := S()\nprint("before")\nfor (i : [1]) { print(c.CALL) }\nprint("after")\n',
    "while": 'c := S()\nprint("before")\ni := 0\nwhile (i < 1) { print(c.CALL); i += 1 }\nprint("after")\n',
    "fn": 'fn(g(v)) { return v.CALL }\nprint("before")\nprint(g(S()))\nprint("after")\n',
    "lambda": 'f := (v) => v.CALL\nprint("before")\nprint(f(S()))\nprint("after")\n',
    "stmt": 'c := S()\nprint("before")\nc.CALL\nprint("after")\n',
    "method": 'c := S()\nprint("before")\nprint(c.probe())\nprint("after")\n',
}


def d009_5(vm, legacy):
    (vo, ve, vr), (lo, le, lr) = vm, legacy
    a = re.match(r"error: t\.f:(\d+):\d+: (.*)\n$", ve)
    b = re.match(r"error: t\.f:(\d+):\d+: return: (.*)\n$", le)
    return vo == lo and vr == lr and a and b and a.groups() == b.groups()


def struct_receivers(nift, work, quick):
    names = set()
    for line in open(os.path.join(ROOT, "src", "rt", "natives", "builtin_list.inc")):
        m = re.match(r'NIFT_RT_BUILTIN\((\w+), "(\w+)", (\w+),', line)
        if m and m.group(3) != "None":
            names.add(m.group(2))
    names |= set(STRUCT_EXTRA)
    stats = {"programs": 0, "vm": 0}
    for ctx, body in STRUCT_CONTEXTS.items():
        for name in sorted(names):
            for args in (("",) if quick else ("", '"a"', "1, 1")):
                call = "%s(%s)" % (name, args)
                head = ("struct(S) {\n  a := 1\n  fn(probe()) { return this.%s }\n}\n" % call if ctx == "method"
                        else "struct(S) { a := 1 }\n")
                src = head + body.replace("CALL", call)
                vm = run_text(nift, src, "require", work)
                stats["programs"] += 1
                label = "struct %s (%s)" % (call, ctx)
                check(vm[2] != 70 and defects(vm[3]) == 0, "%s: exit %d, dispatch_defects %d" %
                      (label, vm[2], defects(vm[3])))
                if not vm[3] or vm[3][-1].get("outcome") not in ("vm", "vm-error"):
                    continue
                stats["vm"] += 1
                legacy = run_text(nift, src, "0", work)
                check(vm[:3] == legacy[:3] or d009_5(vm[:3], legacy[:3]), "%s: VM %r vs legacy %r" %
                      (label, vm[:3], legacy[:3]))
    return stats


def interpolation(nift, work):
    n = 0
    for sk, (sv, shown) in INTERP_SCALARS.items():
        for ok, (ov, text) in INTERP_OTHERS.items():
            for ctx in ("loop", "fn"):
                if ctx == "loop":
                    src = 'print("b")\nfor (v : [%s, %s]) { print("x$[v]") }\nprint("never")\n' % (sv, ov)
                else:
                    src = 'print("b")\nfn(f(v)) { print("x$[v]") }\nf(%s)\nf(%s)\nprint("never")\n' % (sv, ov)
                vo, ve, vr, rows = run_text(nift, src, "require", work)
                n += 1
                label = "interpolation %s|%s (%s)" % (sk, ok, ctx)
                outcome = rows[-1].get("outcome") if rows else None
                if outcome not in ("vm", "vm-error"):
                    continue  # refused before any effect (e.g. interpolation:unknown-type): unchanged
                check(vr == 1 and vo == "b\nx%s\n" % shown and text in ve and "never" not in vo,
                      "%s: %r/%r/%d" % (label, vo, ve[-200:], vr))
                check(defects(rows) == 0, "%s: dispatch_defects %d" % (label, defects(rows)))
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--quick", action="store_true", help="two enumeration contexts only")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    work = tempfile.mkdtemp(prefix="b4-dispatch-")
    try:
        for f in fixtures("dispatch"):
            name = os.path.basename(f)
            src = open(f).read()
            vo, ve, vr, rows = run_text(nift, src, "require", work, name)
            check(rows and rows[-1].get("outcome") in ("vm", "vm-error"),
                  "dispatch %s: not run by the VM: %r" % (name, ve[-300:]))
            check(defects(rows) == 0, "dispatch %s: dispatch_defects %d" % (name, defects(rows)))
            check(vr != 70, "dispatch %s: exit 70" % name)
            check(TAG not in vo + ve and "SECRET-b4" not in vo + ve and "SIDE-EFFECT" not in vo + ve
                  or name.startswith("struct_presentation"),  # explicit calls may run the user method
                  "dispatch %s: an internal tag, a private value or an implicit user method" % name)
            lo, le, lr, _ = run_text(nift, src, "0", work, name)
            golden = f[:-2] + ".vm"
            got = "%s%s[exit %d]\n" % (vo, ve, vr)
            if args.update and os.path.exists(golden):
                open(golden, "w").write(got)
            if os.path.exists(golden):
                check(got == open(golden).read(), "dispatch %s: VM output\n%s\nexpected golden\n%s" %
                      (name, got, open(golden).read()))
                check((vo, ve, vr) != (lo, le, lr), "dispatch %s: no longer differs from legacy" % name)
            else:
                check((vo, ve, vr) == (lo, le, lr), "dispatch %s: VM %r/%r/%d vs legacy %r/%r/%d" %
                      (name, vo, ve, vr, lo, le, lr))
        for f in fixtures("concurrent"):  # deterministic final state; the TSan lane runs them too
            name = os.path.basename(f)
            src = open(f).read()
            for _ in range(3):
                vo, ve, vr, rows = run_text(nift, src, "require", work, name)
                lo, le, lr, _ = run_text(nift, src, "0", work, name)
                check(rows and rows[-1].get("outcome") == "vm" and (vo, ve, vr) == (lo, le, lr),
                      "concurrent %s: VM %r/%r/%d vs legacy %r/%r/%d" % (name, vo, ve[-200:], vr, lo, le, lr))
        # C1 (b4-dispatch.md 7): legacy differential per import-collision shape.
        c1 = os.path.join(HERE, "c1")
        for case in sorted(os.listdir(c1)):
            tree = os.path.join(c1, case)
            expect = open(os.path.join(tree, "expect")).read().strip()
            vo, ve, vr = run_tree(nift, tree, "require", work)
            lo, le, lr = run_tree(nift, tree, "0", work)
            first = ve.splitlines()[0] if ve else ""
            if expect == "program-error":
                check(vr == 1 and vo == "" and first in le.splitlines() and "collides with existing binding" in first
                      and "no part of it ran" in ve and "not compiled" not in ve,
                      "c1 %s: VM %r/%r/%d vs legacy %r/%r/%d" % (case, vo, ve, vr, lo, le, lr))
            else:
                check(vr == 1 and vo == "" and "(import-collision)" in ve,
                      "c1 %s: expected the import-collision refusal, got %r/%r/%d" % (case, vo, ve, vr))
            # Review M2: where legacy stops before the collision (a `legacy` file: lines of its
            # output), it never reports the collision, so the VM must not either.
            if os.path.exists(os.path.join(tree, "legacy")):
                want = open(os.path.join(tree, "legacy")).read().splitlines()
                check(all(line in lo + le for line in want) and "collides with existing binding" not in le,
                      "c1 %s: legacy %r/%r/%d (expected %r, no collision)" % (case, lo, le, lr, want))
        for f in fixtures("refuse"):
            name = os.path.basename(f)
            reason = open(f[:-2] + ".reason").read().strip()
            vo, ve, vr, rows = run_text(nift, open(f).read(), "require", work, name)
            check(vr == 1 and vo == "", "refuse %s: exit %d stdout %r (must be 1 and empty)" % (name, vr, vo))
            check("(%s)" % reason in ve and "no part of it ran" in ve,
                  "refuse %s: expected %s before effects, got %r" % (name, reason, ve.strip()[:300]))
        for name, stdin in SESSIONS:
            vo, ve, vr, rows = run_session(nift, stdin, "require", work)
            lo, le, lr, _ = run_session(nift, stdin, "0", work)
            check(rows and all(r.get("outcome") in ("vm", "vm-error") for r in rows),
                  "session %s: a statement left the VM: %r" % (name, [r.get("reason") for r in rows]))
            check(defects(rows) == 0, "session %s: dispatch_defects %d" % (name, defects(rows)))
            check((vo, ve, vr) == (lo, le, lr), "session %s: VM %r/%r/%d vs legacy %r/%r/%d" %
                  (name, vo, ve, vr, lo, le, lr))
        print("interpolation: %d programs" % interpolation(nift, work))
        for name, spec, out, err in DISPLAYS:
            vo, ve, vr = run_display(nift, spec, work)
            check(vo == out and err in ve and TAG not in vo + ve and "SECRET-b4" not in vo + ve
                  and "SIDE-EFFECT" not in vo and "refused" not in ve and vr != 70,
                  "display %s: VM %r/%r/%d (expected stdout %r, stderr containing %r)" % (name, vo, ve, vr, out, err))
        compile_scaling(nift, work)
        st = struct_receivers(nift, work, args.quick)
        check(st["vm"] >= (60 if args.quick else 300), "struct receivers: only %d programs ran on the VM" % st["vm"])
        print("struct receivers: %(programs)d programs, %(vm)d run by the VM" % st)
        for name, expression, out, err, code in JSON_DISPLAYS:
            got = run_display(nift, ["eval", "--json", expression], work)
            check(got == (out, err, code), "display %s: VM %r (expected %r)" % (name, got, (out, err, code)))
        stats = enumeration(nift, work, args.quick)
        # The enumeration must exercise dynamic dispatch, not only refusals
        # (mutant A3 refuses every Dynamic case).
        check(stats["vm"] >= (200 if args.quick else 400), "enumeration: only %d programs ran on the VM" % stats["vm"])
        print("enumeration: %(programs)d programs, %(vm)d run by the VM, %(refused)d refused" % stats)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if FAILURES:
        print("FAIL b4-dispatch: %d checks, %d failure(s)" % (CHECKS[0], len(FAILURES)))
        return 1
    print("PASS b4-dispatch: %d checks, 0 failure(s)" % CHECKS[0])
    return 0


def scaling_program(n):
    """n functions each using to_int on a double, a map's empty() and an unknown length() (the
    context:* refusal sites; legal outside prepared code), and loops calling another function."""
    lines = ["m := map()"]
    for i in range(n):
        lines += ["fn(h%d(x, v)) {" % i, "    y := x * 1.5", "    t := y.to_int() + (m.empty() ? 1 : 0) + v.length()",
                  "    return t", "}"]
    lines += ["fn(g(v)) { return v + 1 }", "for (i : [1]) { print(g(i)) }", "k := 0",
              "while (k < 1) { k = g(k) }", "print(h0(2, \"ab\") + h%d(2, \"abc\"))" % (n - 1)]
    return "\n".join(lines) + "\n"


def chain_program(n, loop):
    """Re-review N2: a call chain in natural order (h_i calls h_{i-1}), its last function called from a
    `for` or a `while` loop: the prepared marking walks each body once (it advanced one per sweep)."""
    lines = ["fn(h0(x)) { return x }"] + ["fn(h%d(x)) { return h%d(x) + 1 }" % (i, i - 1) for i in range(1, n)]
    if loop == "for":
        lines.append("for (i : [1]) { print(h%d(0)) }" % (n - 1))
    else:
        lines += ["i := 0", "while (i < 1) { print(h%d(0))" % (n - 1), " i += 1 }"]
    return "\n".join(lines) + "\n", "%d\n" % (n - 1)


def compile_scaling(nift, work):
    """Review M3: compile time stays linear in the number of functions (B4's context:* checks once
    walked the whole program per call site: 36x on a 3,451-line program), and in a call chain's
    depth (re-review N2). Four times the functions must cost well under the sixteen times a
    quadratic pass gives; the best of three runs."""
    shapes = {"functions": lambda n: (scaling_program(n), "2\n13\n"),
              "chain-for": lambda n: chain_program(n, "for"), "chain-while": lambda n: chain_program(n, "while")}
    for shape, make in shapes.items():
        times = {}
        for n in (250, 1000):
            src, want = make(n)
            best = None
            for _ in range(3):
                out, _, _, rows = run_text(nift, src, "require", work)
                us = rows[-1].get("compile_us") if rows and rows[-1].get("outcome") == "vm" and out == want else None
                best = us if best is None or (us is not None and us < best) else best
            times[n] = best
        ok = times[250] is not None and times[1000] is not None and times[1000] <= 8 * times[250]
        check(ok, "compile scaling %s: %r us for 250, %r us for 1000 (must be <= 8x)" % (shape, times[250], times[1000]))
        print("compile scaling %s: 250 %.0f us, 1000 %.0f us" % (shape, times[250] or 0, times[1000] or 0))

if __name__ == "__main__":
    sys.exit(main())
