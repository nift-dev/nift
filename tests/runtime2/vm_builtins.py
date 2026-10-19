#!/usr/bin/env python3
"""Runtime 2.0 CP14 wiring (W7, D-016): the built-in supported and
unsupported suites on the VM.

  SUPPORTED    bodies that call every bound CP14 built-in (F1 strings, F2
               arrays and free collection functions, F3 objects, F6
               conversion and formatting) on every admitted receiver shape,
               each in the five CP12 contexts: top level, an `if` block, a
               loop body, a function called from top level and a function
               called from a loop. Each program must compile, run ENTIRELY on
               the VM under NIFT_VM=require and print exactly what legacy
               prints (stdout, stderr, exit status).
  ERRORS       one program per legacy error message a bound native reports,
               at top level (where legacy and the VM report the failing
               expression's position, D-018c): run on the VM with legacy's
               exact output, message and position.
  UNSUPPORTED  one program per wiring refusal (W1-W6, Q3/Q4/Q8/Q10/Q14,
               F4/F5 methods, batch B/C names): refused BEFORE execution with
               the documented reason (NIFT_VM=classify), and under NIFT_VM=1
               the statistics show `fallback` with no VM execution and
               legacy's output.

  vm_builtins.py --nift ./nift [--jobs 2]

Gate (cp14-builtins-design.md, "batch fallback = 0"): no supported program
falls back; every refusal is one of the documented reasons.
"""
import argparse
import concurrent.futures as cf
import json
import os
import shutil
import subprocess
import sys
import tempfile

CONTEXTS = {
    "top": "{body}",
    "if": "if (true) {{\n{body}}}\n",
    "loop": "i0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n{body}}}\n",
    "fn-top": "fn(w0()) {{\n{body}  return 0\n}}\nprint(w0())\n",
    "fn-loop": "fn(w0()) {{\n{body}  return 0\n}}\ni0 := 0\nwhile (i0 < 2) {{ print(w0()); i0 += 1 }}\n",
}

# Bodies valid in every context. Results that are not scalars are printed
# through a binding and stringify() (print takes scalars; a method call on a
# call result is a postfix-receiver refusal).
SUPPORTED = {
    "f1-string": (
        's := "  Héllo, World  "\n'
        "print(s.empty())\n"
        "print(s.trim())\nprint(s.trim_start())\nprint(s.trim_end())\n"
        "print(s.to_upper())\nprint(s.to_lower())\n"
        'print(s.index_of("o"))\nprint(s.last_index_of("o"))\n'
        'print(s.contains("World"))\nprint(s.starts_with("  H"))\nprint(s.ends_with("x"))\n'
        'print(s.replace("o", "0"))\nprint(s.substr(2))\nprint(s.substr(2, 3))\n'
        "print(s.to_string())\n"
        'n := "+42"\nprint(n.to_int())\nd := "2.5e3"\nprint(d.to_double())\n'
        'parts := s.split(",")\nprint(parts.stringify())\n'
        'ab := "ab"\nchars := ab.split("")\nprint(chars.stringify())\n'
    ),
    "f2-array": (
        "a := [3, 1, 2, 1, 2.5]\n"
        "print(a.empty())\nprint(a.first())\nprint(a.last())\n"
        "print(a.indexOf(2))\nprint(a.contains(9))\n"
        'print(a.join("-"))\nprint(a.sum())\nprint(a.min())\nprint(a.max())\n'
        "u := a.unique()\nprint(u.stringify())\n"
        "sl := a.slice(1, 3)\nprint(sl.stringify())\n"
        "tk := a.take(2)\nprint(tk.stringify())\ndr := a.drop(3)\nprint(dr.stringify())\n"
        "ch := a.chunk(2)\nprint(ch.stringify())\n"
        "nest := [[1, 2], [3], 4]\nfl := nest.flatten()\nprint(fl.stringify())\n"
        'es := [{"key": "a", "value": 1}, {"key": 2, "value": [3]}]\nfe := es.from_entries()\nprint(fe.stringify())\n'
        'ws := ["b", "a", "c"]\nprint(ws.min())\nprint(ws.max())\n'
    ),
    "f2-free": (
        "print(min(3, 1, 2))\nprint(max(3, 1, 2))\n"
        "m := [4, 9, 2]\nprint(min(m))\nprint(max(m))\n"
        "r1 := range(4)\nprint(r1.stringify())\nr2 := range(2, 8, 3)\nprint(r2.stringify())\n"
        "r3 := range(5, 0, -2)\nprint(r3.stringify())\n"
    ),
    "f3-object": (
        'o := {"b": 1, "a": [2, 3], "c": {"d": true}}\n'
        "print(o.empty())\n"
        "ks := o.keys()\nprint(ks.stringify())\nvs := o.values()\nprint(vs.stringify())\n"
        "en := o.entries()\nprint(en.stringify())\n"
        'print(o.has("a"))\nprint(o.has("z"))\n'
        'g1 := o.get("a")\nprint(g1.stringify())\nprint(o.get("z", 7))\nprint(o.get("z"))\n'
        'p := {"b": 9, "e": null}\nmg := o.merge(p)\nprint(mg.stringify())\n'
        'deep := {"c": {"x": 1}}\nmd := o.merge_deep(deep)\nprint(md.stringify())\n'
        'pk := o.pick(["a", "c"])\nprint(pk.stringify())\nom := o.omit(["a"])\nprint(om.stringify())\n'
    ),
    "f6-format": (
        'v := {"s": "q<b>", "n": [1, 2.5, null, true]}\n'
        "print(v.stringify())\nprint(v.prettify())\nprint(v.highlight())\n"
        "x := 2.5\nprint(x.to_string())\nt := true\nprint(t.to_string())\nz := null\nprint(z.to_string())\n"
        "big := 0.1 * 3\nprint(big.to_string())\nthird := 1 / 3\nprint(third.to_string())\n"
        'print(html_escape("<a href=x>&</a>"))\nprint(attr_escape("<\'>"))\nprint(url_encode("a b/é"))\n'
        "print(html_escape(3.5))\n"
        'print(type(1))\nprint(type(4 / 2))\nprint(type(2.5))\nprint(type("s"))\nprint(type(v))\nprint(type(null))\n'
        "print(is_null(null))\nprint(is_bool(1))\nprint(is_number(2.5))\nprint(is_int(4 / 2))\nprint(is_float(2.5))\n"
        'print(is_string("s"))\nprint(is_bytes("s"))\nprint(is_array([1]))\nprint(is_object(v))\n'
        "print(is_struct(v))\nprint(is_function(1))\nprint(is_collection(1))\nprint(is_enum(1))\n"
    ),
    "shapes": (
        # Receivers: a binding, a member path, an index; arguments: literals,
        # locals, member and index reads, nested built-in calls.
        'h := {"s": " a,b ", "l": [[1, 2], [3]], "sep": ","}\n'
        "print(h.s.trim())\nprint(h.l[0].sum())\n"
        "q := h.s\nsep := h.sep\nsp := q.split(sep)\nprint(sp.stringify())\n"
        "sp2 := q.split(h.sep)\nprint(sp2.stringify())\n"
        "print(h.l[1].contains(3))\n"
        'w := " x "\nprint(w.trim() + "|" + w.to_upper())\n'
        "print(html_escape(h.s.trim()))\n"
        "head := h.l.first()\nhead.push(9)\nprint(h.l.stringify())\n"
    ),
    "results-in-expressions": (
        'ws := "a,b,c"\nps := ws.split(",")\n'
        "k := 0\nwhile (k < ps.size()) {\n  print(ps[k].to_upper())\n  k += 1\n}\n"
        'if (ws.contains("b")) { print("yes") }\n'
        'total := 0\nnums := range(1, 5)\nj := 0\nwhile (j < nums.length()) { total += nums[j]; j += 1 }\nprint(total)\n'
        'print(ws.length() + ps.length())\n'
    ),
}

# Legacy error messages of bound natives, at top level.
ERRORS = [
    ("substr-pos", 's := "ab"\nprint(s.substr(-1))\n'),
    ("substr-len", 's := "ab"\nprint(s.substr(1, "q"))\n'),
    ("split-delim", 's := "ab"\nr := s.split(1)\nprint(r.size())\n'),
    ("replace-empty", 's := "ab"\nprint(s.replace("", "x"))\n'),
    ("to-int", 's := "4x"\nprint(s.to_int())\n'),
    ("to-double", 's := "0x10"\nprint(s.to_double())\n'),
    ("first-empty", "a := [1]\nb := a.drop(1)\nprint(b.first())\n"),
    ("join-aggregate", 'a := [[1]]\nprint(a.join(","))\n'),
    ("sum-string", 'a := [1, "x"]\nprint(a.sum())\n'),
    ("min-mixed", 'a := [1, "x"]\nprint(a.min())\n'),
    ("chunk-zero", "a := [1]\nc := a.chunk(0)\nprint(c.stringify())\n"),
    ("from-entries", 'a := [{"key": "a"}]\nf := a.from_entries()\nprint(f.stringify())\n'),
    ("range-step", "r := range(1, 5, 0)\nprint(r.stringify())\n"),
    ("range-type", "r := range(1.5)\nprint(r.stringify())\n"),
    ("has-key", 'o := {"a": 1}\nprint(o.has(1))\n'),
    ("merge-arg", 'o := {"a": 1}\nm := o.merge(1)\nprint(m.stringify())\n'),
    ("pick-keys", 'o := {"a": 1}\np := o.pick([1])\nprint(p.stringify())\n'),
    ("html-escape-array", "print(html_escape([1]))\n"),
    # An error in a callee: legacy reports it at the `return` with a
    # `return: ` prefix, the VM at the failing expression (approved D-018c);
    # checked by the policy-classified harnesses (vm_parity, vm_d018), not here.
    ("object-length-top", 'o := {"a": 1}\nprint(o.length())\n'),
]

UNSUPPORTED = [
    # W1 arity (legacy checks it between argument evaluations; Q10).
    ("w1-keys-arity", 'o := {"a": 1}\nk := o.keys(1)\nprint(k.size())\n', "builtin-arity:keys"),
    ("w1-presentation-arity", 'o := {"a": 1}\nprint(o.stringify(1))\n', "builtin-arity:stringify"),
    ("w1-trim-arity", 's := "a"\nprint(s.trim(1))\n', "builtin-arity:trim"),
    # W2 lazy later arguments (Q11).
    ("w2-substr", 'fn(f()) { print("f")\nreturn 1 }\ns := "abc"\nprint(s.substr(1, f()))\n',
     "builtin-lazy-args:substr"),
    ("w2-range", 'fn(f()) { print("f")\nreturn 3 }\nr := range(0, f())\nprint(r.size())\n',
     "builtin-lazy-args:range"),
    ("w2-replace-expr", 's := "abc"\nt := "b"\nprint(s.replace("a", t + "x"))\n', "builtin-lazy-args:replace"),
    # W4 get default.
    ("w4-get-default", 'fn(f()) { print("f")\nreturn 1 }\no := {"a": 1}\nprint(o.get("a", f()))\n',
     "builtin-default:get"),
    # W3 receiver kinds.
    ("w3-mixed", 'fn(f(x)) { return x.empty() }\nprint(f("a"))\nprint(f([1]))\n', "builtin-receiver:empty"),
    ("w3-family", 's := "a"\nprint(s.keys())\n', "builtin-receiver:keys"),
    ("q8-index-of-array", "a := [1]\nprint(a.index_of(1))\n", "builtin-receiver:index_of"),
    ("q4-number-to-double", "x := 2\nprint(x.to_double())\n", "builtin-receiver:to_double"),
    # Q3 object length() in loop code (context).
    ("q3-object-length-loop", 'o := {"a": 1}\ni := 0\nwhile (i < 1) { i += 1\nprint(o.length()) }\n',
     "context:object-length"),
    # Q14 / W6 presentation chain: a method call on a call result is
    # refused first (postfix-receiver); W6 is the defence in depth.
    ("q14-presentation-chain", 'o := {"a": 1}\nprint(o.stringify().stringify())\n', "postfix-receiver"),
    # F4 mutating methods and F5 callbacks: not wired.
    ("f4-pop", "a := [1]\nx := a.pop()\nprint(x)\n", "method:pop"),
    ("f4-sort", "a := [2, 1]\nb := a.sort()\nprint(b.size())\n", "method:sort"),
    # Batch B (CP15) and C (CP16) names.
    ("cp15-abs", "x := -2\nprint(x.abs())\n", "method:abs"),
    ("cp16-replace-once", 's := "aa"\nprint(s.replace_once("a", "b"))\n', "method:replace_once"),
    ("cp17-collection", "s := stack()\nprint(1)\n", "callable:stack"),
]


def env_for(cwd):
    return {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
            "HOME": cwd, "TMPDIR": cwd, **{k: os.environ[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS")
                                          if k in os.environ}}


def run(nift, d, mode, timeout):
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


def check(nift, work, kind, name, source, reason, timeout):
    d = tempfile.mkdtemp(dir=work)
    try:
        with open(os.path.join(d, "p.f"), "w") as f:
            f.write(source)
        legacy, _ = run(nift, d, None, timeout)
        _, cst = run(nift, d, "classify", timeout)
        if kind in ("supported", "error"):
            if not cst or cst["outcome"] != "compiled":
                return f"{name}: not compiled ({cst and cst['reason']})"
            vm, st = run(nift, d, "require", timeout)
            want = "vm" if kind == "supported" else "vm-error"
            if not st or st["outcome"] != want:
                return f"{name}: expected outcome {want}, got {st and (st['outcome'], st['reason'])}"
            if kind == "supported" and legacy[2] != 0:
                return f"{name}: legacy fails the supported program: {legacy}"
            if vm != legacy:
                return f"{name}: output differs\n  legacy {legacy}\n  vm     {vm}"
            return None
        if not cst or cst["outcome"] != "fallback" or not cst["reason"].startswith(reason):
            return f"{name}: expected fallback {reason}, got {cst and (cst['outcome'], cst['reason'])}"
        vm, st = run(nift, d, "1", timeout)
        if not st or st["outcome"] != "fallback" or st.get("vm_executions", 0) != 0:
            return f"{name}: not refused before execution ({st})"
        if vm != legacy:
            return f"{name}: fallback output differs from legacy"
        return None
    finally:
        shutil.rmtree(d, ignore_errors=True)


# Natives whose result outgrows their inputs check the VM's heap budget while
# building (Status::Memory): under a lowered budget the run stops as an
# internal failure (exit 70, never a legacy run), as every VM memory stop.
BUDGET = [
    ("range", "r := range(5000000)\nprint(r.size())\n", 20_000_000),
    ("split-empty", 's := "ab"\ni := 0\nwhile (i < 22) { s += s; i += 1 }\nc := s.split("")\nprint(c.size())\n',
     50_000_000),
]


def budget_checks(nift, work, timeout):
    failures = []
    for name, source, budget in BUDGET:
        d = tempfile.mkdtemp(dir=work)
        with open(os.path.join(d, "p.f"), "w") as f:
            f.write(source)
        env = env_for(d)
        env.update({"NIFT_VM": "require", "NIFT_VM_HEAP_BUDGET": str(budget)})
        p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL,
                           timeout=timeout)
        if p.returncode != 70 or b"memory budget exceeded" not in p.stderr or p.stdout:
            failures.append(f"budget-{name}: expected exit 70 'memory budget exceeded', got {p.returncode} "
                            f"{p.stdout[:60]!r} {p.stderr[-160:]!r}")
        shutil.rmtree(d, ignore_errors=True)
    return failures


def items():
    out = []
    for body_name, body in SUPPORTED.items():
        for ctx, template in CONTEXTS.items():
            indented = "".join("  " + line + "\n" for line in body.splitlines()) if ctx != "top" else body
            out.append(("supported", f"{body_name}@{ctx}", template.format(body=indented), None))
    out += [("error", n, s, None) for n, s in ERRORS]
    out += [("unsupported", n, s, r) for n, s, r in UNSUPPORTED]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--timeout", type=float, default=60)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    todo = items()
    work = tempfile.mkdtemp(prefix="nift-vm-builtins-")
    try:
        with cf.ThreadPoolExecutor(a.jobs) as pool:
            failures = [f for f in pool.map(lambda it: check(nift, work, *it, a.timeout), todo) if f]
        failures += budget_checks(nift, work, a.timeout)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures:
        print("FAIL " + f)
    count = {k: sum(1 for i in todo if i[0] == k) for k in ("supported", "error", "unsupported")}
    print(("PASS" if not failures else "FAIL") + f" vm built-ins: {count['supported']} supported programs "
          f"({len(SUPPORTED)} bodies x {len(CONTEXTS)} contexts, on the VM, identical to legacy), "
          f"{count['error']} error programs, {count['unsupported']} refusals, {len(BUDGET)} budget stops; "
          f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
