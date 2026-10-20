#!/usr/bin/env python3
"""Runtime 2.0 CP11: the two explicit subset suites (review round 2, owner
item 3), so a conservative fallback can never hide a compiler defect.

  SUPPORTED    programs that together exercise every construct of the
               supported subset (docs/runtime-2/checkpoints/cp11-vm-poc.md,
               "Supported subset") at top level, in loop bodies, in functions
               called from top level and in functions called from loops. Each
               must compile, run ENTIRELY on the VM under NIFT_VM=require
               (no fallback, no bail) and print exactly what legacy prints
               (stdout, stderr, exit status).
  UNSUPPORTED  one program per refused construct or context, with the
               expected fallback reason. Each must be refused BEFORE
               execution: NIFT_VM=classify reports `fallback` with that
               reason, and under NIFT_VM=1 the statistics show outcome
               `fallback` with zero VM execution time and the output equals
               legacy's.

  vm_subset.py --nift ./nift [--jobs 2]
"""
import argparse
import concurrent.futures as cf
import json
import os
import shutil
import subprocess
import sys
import tempfile

LOOP = "i0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n{body}}}\n"
FN_TOP = "fn(w0()) {{\n{body}  return 0\n}}\nprint(w0())\n"
FN_LOOP = "fn(w0()) {{\n{body}  return 0\n}}\ni0 := 0\nwhile (i0 < 2) {{ print(w0()); i0 += 1 }}\n"

# Straight-line bodies that are valid in every context.
BODIES = {
    "literals": 'print(1)\nprint(-7)\nprint(2.5)\nprint(.5)\nprint(1e3)\nprint("s")\nprint(\'q\')\n'
                "print(true)\nprint(false)\nprint(null)\nprint(9007199254740991)\nprint(0.0)\nprint(1e300)\n",
    "arithmetic": "print(1 + 2 * 3 - 4 / 8)\nprint(7 % 3)\nprint(-7 % 3)\nprint(0.1 + 0.2)\nprint(10 / 4)\n"
                  'print("a" + 1 + 2.5 + true + null)\nprint(-(3))\nprint(+4)\n',
    "comparison": 'print(1 < 2)\nprint(2 <= 2)\nprint(3 > 4)\nprint(3 >= 4)\nprint(1 == 1.0)\nprint("a" != "b")\n'
                  'print("a" < "b")\nprint([1, 2] == [1, 2])\nprint({"k": 1} == {"k": 1})\n',
    "logic": 'print(1 && "a")\nprint(0 || "")\nprint(!true)\nprint(null ?? 3)\nprint(2 ?? 3)\n'
             'print((1 < 2 ? "y" : "n"))\nprint(!(1 == 2))\n',
    # CP12 (D-018): `%=` on a binding that may hold a fraction is refused in
    # loop code (context:modulo-assignment), so `/=` uses its own binding;
    # review round 2: `/=` may make a fraction, which an int binding rejects
    # (context:assignment-error in loop code), so that binding is a double.
    "bindings": "x := 1\nx = 2\nx += 3\nx -= 1\nx *= 4\nx %= 5\nprint(x)\nz := 8.0\nz /= 2\nprint(z)\n"
                "y := 1.5\ny = 2\n"
                "print(y)\nn := 0\nn++\nn--\n++n\nprint(n)\ns := \"ab\"\ns += \"cd\"\nprint(s)\n",
    "aggregates": 'a := [1, 2, 3]\na.push(4)\na[0] = 9\nprint(a[0] + a.length() + a.size())\n'
                  'o := {"k": [1, {"z": 4}], "n": 2}\nprint(o.k[1].z)\nprint(o["n"])\nw := "héllo"\nprint(w.length())\n',
    "control": "k := 0\nwhile (k < 5) {\n  k += 1\n  if (k == 2) { continue }\n  if (k == 4) { break }\n"
               "  print(k)\n}\nif (k > 10) { print(\"big\") } else if (k > 3) { print(\"mid\") } else { print(\"low\") }\n",
    "block_scope": "q := 1\nif (true) {\n  q := 2\n  print(q)\n}\nprint(q)\n",
    # Review round 3 (F1): element assignment with the admitted index forms,
    # a local name or an unsigned integer literal, computed or not.
    "element_targets": "e := [1, 2, 3]\nj := 2\ne[j] = 7\ne[0] = e[j] + 1\nj = 1\ne[j] = e.length()\n"
                       "e[ j ] = e[0] * 2\nprint(e[0])\nprint(e[1])\nprint(e[2])\n",
    # Review round 3 (F2): every admitted comparison shape (one operator).
    "comparisons_all": 'print(1 < 2.5)\nprint("a" >= "a")\nprint(null == null)\nprint(true != false)\n'
                       'print([1, [2]] != [1, [2]])\nprint(1 == "1")\nprint((1 + 2) <= 3)\n'
                       'print((1 < 2) && (2 > 1))\nprint(!(1 >= 2))\n',
    "decl_from_outer": "g := 2.5\nj := 0\nwhile (j < 1) {\n  h := g\n  h = 1.5\n  print(h)\n  j += 1\n}\n",
    "interpolation": 'x := 7\nprint("v=$[x]!")\n',
    "escapes": 'print("a\\tb")\nprint("q\\\"r")\n',
    "big_integer": 'print(9007199254740993)\n',
}

FUNCTIONS = (
    "fn(add(a, b)) { return a + b }\n"
    "fn(fib(n)) { if (n < 2) { return n } return fib(n - 1) + fib(n - 2) }\n"
    "fn(gcd(a, b)) { while (b != 0) { t := b; b = a % b; a = t } return a }\n"
    "fn(gcd2(a, b)) { t := 0; while (b != 0) { t = b; b = a % b; a = t } return a }\n"
    "fn(nothing()) { }\n"
    "fn(count(xs)) { return xs.length() }\n"
    "fn(greet(s)) { s = s + \"!\"\nreturn s }\n"
    "fn(sum_to(n)) { s := 0; i := 1; while (i <= n) { s += i; i += 1 } return s }\n"
    "fn(even(n)) { if (n == 0) { return true } return odd(n - 1) }\n"
    "fn(odd(n)) { if (n == 0) { return false } return even(n - 1) }\n"
)
CALLS = ('print(add(2, 3))\nprint(add("a", 1))\nprint(fib(15))\nprint(gcd(1071, 462))\nprint(nothing())\n'
         'print(count([1, 2, 3]))\nprint(greet("world"))\nprint(sum_to(100))\nprint(even(10))\n'
         'print(gcd2(1071, 462))\n')


def supported():
    out = []
    for name, body in BODIES.items():
        indented = "".join("  " + l + "\n" for l in body.splitlines())
        out.append((f"{name}-top", body))
        if name != "decl_from_outer":  # its own loop is the prepared context
            out.append((f"{name}-loop", LOOP.format(body=indented)))
            out.append((f"{name}-fn-top", FN_TOP.format(body=indented)))
            out.append((f"{name}-fn-loop", FN_LOOP.format(body=indented)))
    out.append(("functions-top", FUNCTIONS + CALLS))
    # gcd declares `t := b` (a binding reference): refused in loop-called functions.
    loop_fns = "".join(l + "\n" for l in FUNCTIONS.splitlines() if not l.startswith("fn(gcd("))
    loop_calls = "".join(l + "\n" for l in CALLS.splitlines() if "gcd(" not in l)
    out.append(("functions-loop", loop_fns + "i0 := 0\nwhile (i0 < 2) {\n" + loop_calls + "i0 += 1\n}\n"))
    out.append(("functions-condition", FUNCTIONS + "k := 0\nwhile (fib(k) < 50) { k += 1 }\nprint(k)\n"))
    # gcd in loop contexts (its loop-safe form declares from a literal).
    out.append(("gcd-loop", FUNCTIONS + "k := 0\nwhile (k < 3) { k += 1\nprint(gcd2(1071 * k, 462)) }\n"
                            "while (gcd2(k, 6) < 3) { k += 1 }\nprint(k)\n"))
    # CP12 phase B: declarations after statements (no call can run first).
    out.append(("fn-after-statements", "x := 2\nprint(x)\nfn(sq(n)) { return n * n }\nprint(sq(x))\n"
                                       "fn(cube(n)) { return n * sq(n) }\nk := 0\n"
                                       "while (k < 2) { k += 1\nprint(cube(k)) }\n"))
    out.append(("top-return", "x := 1\nwhile (x < 9) { x += 1; if (x == 3) { return x } }\n"))
    out.append(("bench-call", "fn(add(a, b)) { return a + b }\ni := 0\ns := 0\n"
                              "while (i < 2000) { s = add(s, i); i += 1 }\nprint(s)\n"))
    out.append(("bench-fib-loop", "fn(fib(n)) { if(n < 2) { return n } return fib(n - 1) + fib(n - 2) }\n"
                                  "i := 0\nwhile(i < 1) { print(fib(18)); i += 1 }\n"))
    out.append(("deep-equality", "fn(w(p)) { return [p ?? 0] }\nx := [1]\ny := [1]\ni := 0\n"
                                 "while (i < 500) { x = w(x); y = w(y); i += 1 }\nprint(x == y)\n"))
    return out


# (name, program, expected fallback reason prefix)
UNSUPPORTED = [
    # Statements and declarations outside the subset.
    ("const", "const c := 1\nprint(c)\n", "declare-const"),
    ("destructuring", "[a, b] := [1, 2]\nprint(a)\n", "declare-pattern"),
    ("for", "for (x : [1, 2]) { print(x) }\n", "statement:For"),
    ("try", "try { print(1) } catch (e) { print(2) }\n", "statement:Try"),
    ("struct", "struct (P) { x := 1 }\nprint(1)\n", "statement:StructDecl"),
    ("fn-called-before-declaration", "print(f())\nfn(f()) { return 1 }\n", "fn-called-before-declaration"),
    ("fn-called-before-declaration-transitive", "fn(g()) { return f() }\nprint(g())\nfn(f()) { return 1 }\n",
     "fn-called-before-declaration"),
    ("async-fn", "fn[async](f()) { return 1 }\nprint(1)\n", "fn-flags"),
    ("lambda", "f := (x) => x\nprint(1)\n", "expression:Lambda"),
    ("bare-expression", "x := 1\nx\n", "expression-statement"),
    ("jump-not-last", "i := 0\nwhile (i < 3) { i += 1; break; print(i) }\n", "jump-not-last"),
    ("jump-outside-loop", "break\n", "jump-outside-loop"),
    # CP12 (D-018): legacy behaviour that depends on the calling context.
    ("context-assignment", "x := 1\ni := 0\nwhile (i < 2) { x = x + 0.5; i += 1 }\n", "context:assignment-type"),
    ("context-modulo", "x := 7\nx /= 2\ni := 0\nwhile (i < 1) { x %= 2; i += 1 }\nprint(x)\n",
     "context:modulo-assignment"),
    ("context-assignment-error", "z := 8\ni := 0\nwhile (i < 2) { z /= 2; i += 1 }\nprint(z)\n",
     "context:assignment-error"),
    ("context-assignment-error-kind", "x := 0\ni := 0\nwhile (i < 1) { x = \"s\"; i += 1 }\n",
     "context:assignment-error"),
    ("context-negative-zero", "x := 0\ni := 0\nwhile (i < 1) { print(-x); i += 1 }\n", "context:negative-zero"),
    ("context-object-length", "fn(n(o)) { return o.length() }\ni := 0\nwhile (i < 1) { print(n({\"k\": 1})); "
                              "i += 1 }\n", "context:object-length"),
    ("context-string-append", "s := \"a\" + 0.5\ni := 0\nwhile (i < 1) { s += \"b\"; i += 1 }\nprint(s)\n",
     "context:string-append-type"),
    ("context-parameter", "fn(f(p)) { p = 1\nreturn p }\ni := 0\nwhile (i < 1) { print(f(2.0 * i)); i += 1 }\n",
     "context:parameter-type"),
    # Review round 3 (F1): only a local name or an unsigned integer literal index.
    ("index-target-call", "fn(f()) { return 0 }\na := [1]\na[f()] = f()\n", "index-target"),
    ("index-target-dot", "a := [1, 2, 3]\na[a.length() - 1] = 9\nprint(a[2])\n", "index-target"),
    ("index-target-member", 'a := [1, 2]\no := {"k": 1}\na[o.k] = 9\nprint(a[1])\n', "index-target"),
    ("index-target-decimal", "a := [1, 2]\na[1.0] = 9\nprint(a[1])\n", "index-target"),
    ("index-target-string-arg", 'fn(g(x)) { return 0 }\na := [1]\na[g("x.y")] = 9\nprint(a[0])\n',
     "index-target"),
    ("index-target-paren", "a := [1, 2]\ni := 1\na[(i)] = 9\nprint(a[1])\n", "index-target"),
    ("index-target-arith", "a := [1, 2]\ni := 0\na[i + 1] = 9\nprint(a[1])\n", "index-target"),
    ("index-compound", "a := [1, 2]\na[0] += 1\nprint(a[0])\n", "compound-target"),
    ("index-increment", "a := [1, 2]\ni := 0\na[i]++\nprint(a[0])\n", "incdec-target"),
    # Review round 3 (F2): no comparison whose operand is a comparison.
    ("comparison-chain-eq", "print(1 != 1 == 1)\n", "comparison-chain"),
    ("comparison-chain-paren", "x := 1\nprint((x < 2) == true)\n", "comparison-chain"),
    ("comparison-chain-right", "x := 1\nif (true == (x >= 2)) { print(1) }\n", "comparison-chain"),
    # Review round 3 (F3): tree depth bounded before any analysis.
    ("nesting-limit", "print(" + " + ".join(["1"] * 1000) + ")\n", "nesting-limit"),
    ("free-name", "fn(f()) { return n }\nn := 1\nprint(f())\n", "free-name"),
    ("unresolved-name", "print(args)\n", "unresolved-name"),
    # Literals.
    ("leading-zero", "print(01)\n", "number-literal"),
    ("hex-literal", "print(0x10)\n", "opaque:legacy-runtime-error"),
    ("overflow-exponent", "print(1e309)\n", "number-literal"),
    ("escaped-dollar", 'print("a\\$[1]")\n', "opaque:irregular-legacy-parse"),
    # Names legacy dispatches specially.
    ("builtin-fn-name", "fn(is_null(x)) { return 1 }\nprint(is_null(2))\n", "fn-name-reserved"),
    ("reserved-binding", "title := 1\nprint(title)\n", "binding-name-reserved"),
    ("reserved-param", "fn(f(name)) { return name }\nprint(f(1))\n", "param-name-reserved"),
    # Methods and natives.
    # CP14 wiring: split is a bound built-in; replace_once is batch C (CP16).
    ("method", 'x := "a,b"\nprint(x.replace_once("a", "b"))\n', "method:replace_once"),
    ("native", "print(stringify(1))\n", "callable:stringify"),
    ("alias-argument", 'fn(f(p)) { p.push(9) }\no := {"k": [1]}\nf(o.k)\n', "alias-argument"),
    # Statically known errors (review round 2: refused, no longer replayed).
    ("redeclared", "x := 1\nx := 2\n", "static-error:redeclared"),
    ("arity", "fn(f(a)) { return a }\nprint(f())\n", "static-error:arity"),
    ("member-assign", 'o := {"k": 1}\no.k = 2\n', "static-error:member-assign"),
    ("push-arity", "a := [1]\na.push(1, 2)\n", "static-error:push-arity"),
    # Parser (CP10): Opaque shapes.
    ("bang-precedence", "x := 2\nprint(!x + 1)\n", "opaque:irregular-legacy-parse"),
    ("postfix-space", "a := [1]\nprint(a [0])\n", "opaque:legacy-runtime-error"),
    ("form-feed", "print(1)\f\n", "opaque:legacy-runtime-error"),
    ("vertical-tab", "x := 2.5\v\nprint(x)\n", "opaque:legacy-runtime-error"),
    ("ternary-in-condition", "x := 1\nif ((x > 0 ? true : false)) { print(1) }\n", "opaque:irregular-legacy-parse"),
    # Legacy's prepared evaluator (review round 2: M1, N1, M2, M3).
    ("prepared-decl-shadow", 'a := "s"\ni := 0\nwhile (i < 1) {\ni += 1\na := 2.5\nb := a\nb = 1.5\nprint(b)\n}\n',
     "prepared-decl-type"),
    ("prepared-decl-paren", 'a := "s"\ni := 0\nwhile (i < 1) {\ni += 1\na := 2.5\nb := (a)\nprint(b)\n}\n',
     "prepared-decl-type"),
    ("prepared-decl-value", "i := 0\nwhile (i < 1) { i += 1; a := 2.0; b := a; b = 2.5 }\n", "prepared-decl-type"),
    ("prepared-decl-param", 'fn(f(a)) {\nb := a\nb = 1.5\nreturn b\n}\na := "s"\ni := 0\n'
                            "while (i < 1) { print(f(2.5)); i += 1 }\n", "prepared-decl-type"),
    ("prepared-decl-nested-call", "fn(f(a)) { b := a * 1\nreturn b }\nfn(g(a)) { return f(2.5) }\ni := 0\n"
                                  "while (i < 1) { print(g(7)); i += 1 }\n", "prepared-decl-type"),
    ("prepared-fn-ternary-return", 'fn(g(x)) { print("g")\nreturn x }\nfn(f(p)) { return (true ? p : 0) }\n'
                                   "c := 0\nwhile (c < 1) { c += 1\nf(g(1)) }\n", "prepared-fn-unprepared"),
    # Stage 4 (records20k): with literal or name-read arguments these calls are
    # admitted (vm_records_coverage.py); an argument with an effect stays refused.
    ("prepared-fn-chained-compare", 'fn(g(x)) { print("g")\nreturn x }\n'
                                    "fn(f(p)) { if (p < 2 == true) { print(1) }\nreturn 0 }\nc := 0\n"
                                    "while (c < 1) { c += 1\nf(g(1)) }\n", "prepared-fn-unprepared"),
    ("prepared-fn-bare-return", 'fn(g(x)) { print("g")\nreturn x }\n'
                                "fn(f(p)) { if (p > 0) { return }\nreturn 1 }\nc := 0\n"
                                "while (c < 1) { c += 1\nprint(f(g(1))) }\n", "prepared-fn-unprepared"),
    ("prepared-fn-paren-coalesce", 'fn(g(x)) { print("g")\nreturn x }\n'
                                   "fn(f(p)) { return (p ?? 0) }\nc := 0\nwhile (f(g(c)) < 1) { c += 1 }\n",
     "prepared-fn-unprepared"),
    ("string-arg-name", 'fn(f(s)) {\ns = "7"\nreturn 0\n}\nq := null\nprint(f("q"))\n', "string-arg-type"),
    ("string-arg-number", 'fn(f(n)) {\nn = "x"\nreturn n\n}\nprint(f("2.5"))\n', "string-arg-type"),
    ("string-arg-caller-local", 'fn(f(s)) { return s }\nfn(g(q)) { return f("q") }\nprint(g(1))\n',
     "string-arg-type"),
]


def env_for(cwd):
    return {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
            "HOME": cwd, "TMPDIR": cwd, **{k: os.environ[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS")
                                          if k in os.environ}}


def run(nift, d, mode, timeout=60):
    env = env_for(d)
    stats = os.path.join(d, ".stats")
    if os.path.exists(stats):
        os.unlink(stats)
    if mode:
        env.update({"NIFT_VM": mode, "NIFT_VM_STATS": stats})
    p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL, timeout=timeout)
    st = None
    if mode and os.path.exists(stats):
        lines = open(stats).read().splitlines()
        st = json.loads(lines[-1]) if lines else None
    return (p.stdout, p.stderr, p.returncode), st


def check(nift, work, kind, name, source, reason):
    d = tempfile.mkdtemp(dir=work)
    try:
        with open(os.path.join(d, "p.f"), "w") as f:
            f.write(source)
        legacy, _ = run(nift, d, None)
        _, cst = run(nift, d, "classify")
        if kind == "supported":
            if not cst or cst["outcome"] != "compiled":
                return f"{name}: not compiled ({cst and cst['reason']})"
            vm, st = run(nift, d, "require")
            if not st or st["outcome"] != "vm":
                return f"{name}: did not run entirely on the VM ({st})"
            if vm != legacy:
                return f"{name}: output differs\n  legacy {legacy}\n  vm     {vm}"
            return None
        if not cst or cst["outcome"] != "fallback" or not cst["reason"].startswith(reason):
            return f"{name}: expected fallback {reason}, got {cst and (cst['outcome'], cst['reason'])}"
        vm, st = run(nift, d, "1")
        if not st or st["outcome"] != "fallback" or st.get("execute_us", 0) != 0:
            return f"{name}: not refused before execution ({st})"
        if vm != legacy:
            return f"{name}: fallback output differs from legacy"
        return None
    finally:
        shutil.rmtree(d, ignore_errors=True)


# Review round 3 (F4): `==`/`!=` on large aggregates must stay within the VM
# memory budget. The comparison used to build a work list of every element
# pair outside the budget (73 MB peak for two 1M-element arrays under a 50 MB
# budget); it now keeps one frame per nesting level. The peak RSS of the
# whole process must stay below the budget plus a fixed allowance for the
# process itself (binary, allocator, stacks).
EQUALITY_MEMORY = [
    ("flat", "a := [0]\nb := [0]\ni := 0\nwhile (i < 1000000) { a.push(i); b.push(i); i += 1 }\n"
             "print(a == b)\nprint(a != b)\n", "true\nfalse\n"),
    ("nested", "fn(w(p, n)) { return [p ?? 0, [n, n + 1]] }\na := [0]\nb := [0]\ni := 0\n"
               "while (i < 9000) { a = w(a, i); b = w(b, i); i += 1 }\nprint(a == b)\nprint(a != b)\n",
     "true\nfalse\n"),
]
EQUALITY_BUDGET = 50_000_000
PROCESS_ALLOWANCE = 8 << 20


def equality_memory(nift, work):
    failures = []
    if not hasattr(os, "wait4") or "ASAN_OPTIONS" in os.environ:
        return failures  # RSS is meaningless under ASan
    for name, source, expected in EQUALITY_MEMORY:
        d = tempfile.mkdtemp(dir=work)
        with open(os.path.join(d, "p.f"), "w") as f:
            f.write(source)
        env = env_for(d)
        env.update({"NIFT_VM": "require", "NIFT_VM_HEAP_BUDGET": str(EQUALITY_BUDGET)})
        out_path, err_path = os.path.join(d, ".out"), os.path.join(d, ".err")
        with open(out_path, "wb") as o, open(err_path, "wb") as e:
            p = subprocess.Popen([nift, "p.f"], cwd=d, env=env, stdout=o, stderr=e, stdin=subprocess.DEVNULL)
            _, status, usage = os.wait4(p.pid, 0)  # this child's own peak RSS
            p.returncode = os.waitstatus_to_exitcode(status)
        out, err = open(out_path, "rb").read(), open(err_path, "rb").read()
        rss = usage.ru_maxrss * 1024
        if p.returncode != 0 or out.decode() != expected:
            failures.append(f"equality-memory-{name}: exit {p.returncode}, out {out[:60]!r}, err {err[-200:]!r}")
        elif rss > EQUALITY_BUDGET + PROCESS_ALLOWANCE:
            failures.append(f"equality-memory-{name}: peak RSS {rss} bytes exceeds the budget "
                            f"{EQUALITY_BUDGET} + {PROCESS_ALLOWANCE}")
        shutil.rmtree(d, ignore_errors=True)
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--jobs", type=int, default=2)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    items = [("supported", n, s, None) for n, s in supported()] + [("unsupported", n, s, r) for n, s, r in UNSUPPORTED]
    work = tempfile.mkdtemp(prefix="nift-vm-subset-")
    try:
        with cf.ThreadPoolExecutor(a.jobs) as pool:
            failures = [f for f in pool.map(lambda it: check(nift, work, *it), items) if f]
        failures += equality_memory(nift, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures:
        print("FAIL " + f)
    n_sup = sum(1 for i in items if i[0] == "supported")
    print(("PASS" if not failures else "FAIL") + f" vm subset suites: {n_sup} supported programs (NIFT_VM=require, "
          f"identical to legacy), {len(items) - n_sup} unsupported programs (refused before execution); "
          f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
