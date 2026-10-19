#!/usr/bin/env python3
"""records20k coverage (integration stage 4, coordinator ruling): the two
CP12 refusals lifted with their safety conditions, as differential suites.

  SUPPORTED   shapes now compiled, each run under NIFT_VM=require and compared
              with legacy byte for byte (stdout, stderr, exit status):
              * a loop call to a function legacy cannot prepare (bare
                `return`; `?:`/`??`/chained comparison in a `return`, `if` or
                `while` condition) whose arguments are literals or name reads:
                legacy evaluates them twice (M2) but that cannot be observed,
                and the body runs once on its canonical path;
              * `a.push(v)` of a fresh aggregate (call result, object/array
                literal, `?:` of literals) at top level, in loops and in
                functions called from loops; mutation after the push.
  REFUSED     the shapes whose difference is observable stay refused before
              execution with the reason: an argument with an effect
              (prepared-fn-unprepared), a pushed aggregate taken from a place
              (aliasing: legacy pushes a location reference), a declaration
              in a prepared function from any text but one parameter name
              (prepared-decl-type).
  PARAM_DECL  the third lift (coordinator ruling on `pad`): in a prepared
              function, `x := p` (p a parameter) takes p's bound type from the
              type register. The same function called with string, int,
              double, array and object arguments, from loops and from top
              level, with later reassignments of the declared binding to
              another type: NIFT_VM=1 (any engine path) must give legacy's
              output byte for byte, including legacy's binding-type errors;
              the ones marked `vm` must run on the VM. The top-level `1e2`
              case (a double-typed binding holding an integral value) binds
              differently if the register is ignored (mutant 9).
              Independent oracle (owner): parity with legacy is not enough if
              both share a defect, so each program also states, from the
              arguments alone, the printed lines (the value passed, as print
              and stringify render it) and whether a binding-type error
              occurs: the declared binding takes the passed value's type, and
              a reassignment fails exactly when the new value's type differs
              (an int into a double binding is a number: accepted). Legacy
              and NIFT_VM=1 must both match it, with the declaration typed by
              its text on every path (D-046 #1). Known legacy inconsistency
              L52: in prepared (loop) code legacy types a parameter copy by
              the value; there the oracle states the intended result, legacy's
              actual output is pinned, and the VM must refuse the program so
              NIFT_VM=1 stays byte-identical to legacy.

  vm_records_coverage.py --nift ./nift
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

SUPPORTED = [
    ('push_arrlit_fnloop', 'fn(add(a, i)) { a.push([i, i + 1]) \nreturn a.length() }\na := []\ni := 0\nwhile (i < 2) { add(a, i); i += 1 }\nprint(a.stringify())\n'),
    ('push_call_loop', 'fn(mk(i)) { return {"id": i, "t": [i]} }\na := []\ni := 0\nwhile (i < 3) { a.push(mk(i)); i += 1 }\nprint(a.stringify())\nprint(a[2].t[0] + a[1].id)\n'),
    ('push_call_top', 'fn(mk(i)) { return {"id": i, "t": [i]} }\na := []\na.push(mk(1))\nb := mk(2)\na.push(b.t)\nprint(a.stringify())\n'),
    ('push_objlit_loop', 'a := []\ni := 0\nwhile (i < 3) { a.push({"k": i, "s": "v" + i, "l": [i, i]}); i += 1 }\nprint(a.stringify())\n'),
    ('push_records_shape', 'fn(make_record(i)) {\n  return {"id": i, "name": "item" + i, "score": (i * 37) % 101, "tags": [i % 3, i % 5]}\n}\nrecords := []\ni := 0\nwhile (i < 4) {\n  records.push(make_record(i))\n  i += 1\n}\nprint(records.stringify())\nprint(records[3].tags[1] + records[2].score)\n'),
    ('push_ternary_lit', 'a := []\ni := 0\nwhile (i < 2) { a.push((i > 0 ? [1] : [2])); i += 1 }\nprint(a.stringify())\n'),
    ('push_then_mutate', 'fn(mk(i)) { return [i] }\na := []\nv := mk(5)\na.push(mk(1))\ni := 0\nwhile (i < 1) { a.push(mk(2)); i += 1 }\nx := a[0]\nprint(a.stringify())\n'),
    ('up_bare_return', 'fn(f(p)) { if (p > 0) { print("pos")\nreturn }\nprint("zero") }\ni := 0\nwhile (i < 2) { f(i); i += 1 }\n'),
    ('up_chain_if', 'fn(f(p)) { if (p < 2 == true) { print("lt") }\nreturn p }\ni := 0\nwhile (i < 3) { print(f(i)); i += 1 }\n'),
    ('up_coalesce', 'fn(f(p)) { return (p ?? 1) }\ni := 0\nwhile (i < 2) { print(f(i)); i += 1 }\n'),
    ('up_fn_from_loop_fn', 'fn(f(p)) { return (true ? p * 2 : 0) }\nfn(g(q)) { return f(q) + 1 }\ni := 0\nwhile (i < 2) { print(g(i)); i += 1 }\n'),
    ('up_neg_lit', 'fn(f(p)) { return (true ? p : 0) }\ni := 0\nwhile (i < 1) { print(f(-3)); i += 1 }\n'),
    ('up_object_str', 'fn(mk(i)) { return {"id": i, "name": "item" + i} }\ni := 0\ns := ""\nwhile (i < 3) { o := mk(i); s += o.name; i += 1 }\nprint(s)\n'),
    ('up_string_lit', 'fn(f(p)) { return (true ? p : "z") }\ni := 0\nwhile (i < 1) { print(f("ab")); i += 1 }\n'),
    ('up_ternary_lit', 'fn(f(p)) { print("body " + p)\nreturn (true ? p : 0) }\ni := 0\nwhile (i < 2) { x := f(1); print(x); i += 1 }\n'),
    ('up_ternary_local', 'fn(f(p)) { print("body " + p)\nreturn (true ? p : 0) }\ni := 0\nwhile (i < 2) { x := f(i); print(x); i += 1 }\n'),
    ('up_while_cond', 'fn(f(p)) { return (p > 0 ? 1 : 0) }\ni := 0\nwhile (i < 3 && f(i) < 5) { print(i); i += 1 }\n'),
]

PARAM_DECL = [
    # (name, must run on the VM, source); the oracle is ORACLE[name]
    ("pd_types_loop", True, 'fn(f(s)) {\n  out := s\n  print(out.stringify())\n  return 0\n}\n'
     'vals := ["a", 2, 2.5, [1, "b"], {"k": 1}]\ni := 0\nwhile (i < 5) { f(vals[i]); i += 1 }\nf("top")\nf(7)\n'),
    ("pd_types_literals", True, 'fn(f(s)) {\n  out := s\n  print(out.stringify())\n  return 0\n}\ni := 0\n'
     'while (i < 1) { f("a"); f(2); f(2.5); f([1, 2]); f({"k": 1}); i += 1 }\n'),
    ("pd_pad", True, 'fn(pad(s, width)) {\n  out := s\n  while (out.length() < width) { out += "." }\n  return out\n}\n'
     'names := ["item0", "it", "x"]\nk := 0\nr := ""\nwhile (k < 3) { r += pad(names[k], 6) + ";"; k += 1 }\n'
     'print(r)\nprint(pad("top", 5))\n'),
    ("pd_register_type_top", True, 'fn(f(p)) {\n  out := p\n  print(out)\n  out = 2.5\n  print(out)\n  return 0\n}\n'
     'x := 1e2\nprint(x)\nf(x)\nv := 100\nf(v)\n'),
    ("pd_reassign_double_then_string", False, 'fn(f(s)) {\n  out := s\n  out = 1.5\n  print(out)\n  return 0\n}\n'
     'i := 0\nwhile (i < 1) { f(2.5); i += 1 }\nwhile (i < 2) { f("a"); i += 1 }\n'),
    ("pd_reassign_double_then_int", False, 'fn(f(s)) {\n  out := s\n  out = 1.5\n  print(out)\n  return 0\n}\n'
     'i := 0\nwhile (i < 1) { f(2.5); i += 1 }\nwhile (i < 2) { f(3); i += 1 }\n'),
    ("pd_reassign_string_loop_top", False, 'fn(f(s)) {\n  out := s\n  out = "x"\n  print(out)\n  return 0\n}\n'
     'i := 0\nwhile (i < 1) { f("a"); i += 1 }\nf("b")\nwhile (i < 2) { f(3); i += 1 }\n'),
    ("pd_reassign_untyped_args", False, 'fn(f(s)) {\n  out := s\n  out = 7\n  print(out)\n  return 0\n}\n'
     'vals := [2.5, 3, "z"]\ni := 0\nwhile (i < 3) { f(vals[i]); i += 1 }\n'),
    ("pd_register_type_loop", False, 'fn(f(p)) {\n  out := p\n  print(out)\n  out = 2.5\n  print(out)\n  return 0\n}\n'
     'x := 1e2\ni := 0\nwhile (i < 1) { f(x); i += 1 }\nv := 100\nwhile (i < 2) { f(v); i += 1 }\n'),
    ("pd_reassign_top_only", True, 'fn(f(s)) {\n  out := s\n  out = 1.5\n  print(out)\n  return 0\n}\nf(2.5)\nf("a")\n'),
]

# Known legacy inconsistencies: name -> (register entry, legacy's actual
# printed lines, legacy ends with an error). The VM must refuse these programs.
LEGACY_INCONSISTENT = {
    'pd_register_type_loop': ('L52', ['100'], True),
}

# name -> (printed lines, a binding-type error ends the run), derived from the
# arguments by the rule above (pd_pad: s padded with "." to the width).
ORACLE = {
    'pd_types_loop': (['"a"', '2', '2.5', '[1,"b"]', '{"k":1}', '"top"', '7'], False),
    'pd_types_literals': (['"a"', '2', '2.5', '[1,2]', '{"k":1}'], False),
    'pd_pad': (['item0.;it....;x.....;', 'top..'], False),
    # Textual declaration typing (D-046 #1): x := 1e2 is a double binding
    # holding 100; copying the parameter keeps double, so `out = 2.5` is
    # accepted on the first call and fails on the int call.
    'pd_register_type_top': (['100', '100', '2.5', '100'], True),
    'pd_reassign_double_then_string': (['1.5'], True),
    'pd_reassign_double_then_int': (['1.5'], True),
    'pd_reassign_string_loop_top': (['x', 'x'], True),
    'pd_reassign_untyped_args': (['7', '7'], True),
    # The same in loop code: the intended result (D-046 #1); legacy differs
    # here (L52, LEGACY_INCONSISTENT).
    'pd_register_type_loop': (['100', '2.5', '100'], True),
    'pd_reassign_top_only': (['1.5'], True),
}

REFUSED = [
    ("decl-param-expression", 'fn(f(s)) {\n  out := s + ""\n  return out\n}\ni := 0\nwhile (i < 1) { print(f("a")); i += 1 }\n',
     "prepared-decl-type"),
    ("effect-argument", 'fn(g(x)) { print("g " + x)\nreturn x }\nfn(f(p)) { return (true ? p : 0) }\n'
     'i := 0\nwhile (i < 1) { print(f(g(i))); i += 1 }\n', "prepared-fn-unprepared"),
    ("call-argument", 'fn(g(x)) { return x }\nfn(f(p)) { return (p ?? 0) }\ni := 0\n'
     'while (i < 1) { print(f(g(1))); i += 1 }\n', "prepared-fn-unprepared"),
    ("expression-argument", 'fn(f(p)) { return (p ?? 0) }\ni := 0\nwhile (i < 1) { print(f(i + 1)); i += 1 }\n',
     "prepared-fn-unprepared"),
    ("push-binding", 'o := {"k": 1}\na := []\na.push(o)\nprint(a.stringify())\n', "aliasing"),
    ("push-member", 'o := {"k": [1]}\na := []\na.push(o.k)\nprint(a.stringify())\n', "aliasing"),
    ("push-index-loop", 'a := [[1]]\nb := []\ni := 0\nwhile (i < 1) { b.push(a[0]); i += 1 }\nprint(b.stringify())\n',
     "aliasing"),
]


def run(nift, d, mode, timeout):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "HOME": d,
           "TMPDIR": d, **{k: os.environ[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS") if k in os.environ}}
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--timeout", type=float, default=60)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    failures = []
    work = tempfile.mkdtemp(prefix="nift-vm-records-")
    try:
        for kind, cases in (("supported", [(n, s, None) for n, s in SUPPORTED]), ("refused", REFUSED)):
            for name, source, reason in cases:
                d = tempfile.mkdtemp(dir=work)
                with open(os.path.join(d, "p.f"), "w") as f:
                    f.write(source)
                legacy, _ = run(nift, d, None, a.timeout)
                if kind == "supported":
                    vm, st = run(nift, d, "require", a.timeout)
                    if not st or st["outcome"] not in ("vm", "vm-error"):
                        failures.append(f"{name}: not on the VM ({st and (st['outcome'], st['reason'])})")
                    elif vm != legacy:
                        failures.append(f"{name}: output differs\n  legacy {legacy}\n  vm     {vm}")
                else:
                    _, cst = run(nift, d, "classify", a.timeout)
                    if not cst or cst["outcome"] != "fallback" or cst["reason"] != reason:
                        failures.append(f"{name}: expected refused ({reason}), got {cst and (cst['outcome'], cst['reason'])}")
                    else:
                        vm, st = run(nift, d, "1", a.timeout)
                        if vm != legacy or not st or st.get("vm_executions", 0) != 0:
                            failures.append(f"{name}: not refused before execution or output differs")
                shutil.rmtree(d, ignore_errors=True)
        for name, vm_required, source in PARAM_DECL:
            d = tempfile.mkdtemp(dir=work)
            with open(os.path.join(d, "p.f"), "w") as f:
                f.write(source)
            legacy, _ = run(nift, d, None, a.timeout)
            auto, st = run(nift, d, "1", a.timeout)
            outcome = st and st.get("outcome")
            lines, error = ORACLE[name]
            want = ("\n".join(lines) + "\n").encode()
            checked = (("legacy", legacy), ("NIFT_VM=1", auto))
            if name in LEGACY_INCONSISTENT:
                entry, legacy_lines, legacy_error = LEGACY_INCONSISTENT[name]
                legacy_want = ("\n".join(legacy_lines) + "\n").encode()
                if legacy[0] != legacy_want or (legacy[2] != 0) != legacy_error:
                    failures.append(f"{name}: legacy no longer shows {entry} ({legacy[0]!r}, exit {legacy[2]})")
                if outcome in ("vm", "vm-error"):
                    failures.append(f"{name}: {entry} case must be refused, ran on the VM ({outcome})")
                checked = ()
            for label, got in checked:
                if got[0] != want or (got[2] != 0) != error or (error and b"binding" not in got[1]):
                    failures.append(f"{name}: {label} differs from the oracle: stdout {got[0]!r} exit {got[2]} "
                                    f"stderr {got[1][-120:]!r}; oracle {want!r} error {error}")
            if auto != legacy:
                failures.append(f"{name}: NIFT_VM=1 output differs ({outcome})\n  legacy {legacy}\n  auto   {auto}")
            elif vm_required and outcome not in ("vm", "vm-error"):
                failures.append(f"{name}: not on the VM ({outcome}, {st and st.get('reason')})")
            print(f"param-decl {name}: {outcome} {st and st.get('reason')!r}, identical to legacy: {auto == legacy}, "
                  f"oracle: {ORACLE[name]}")
            shutil.rmtree(d, ignore_errors=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures:
        print("FAIL " + f)
    print(("PASS" if not failures else "FAIL") + f" vm records coverage: {len(SUPPORTED)} supported shapes on the VM "
          f"(identical to legacy), {len(REFUSED)} refused, {len(PARAM_DECL)} parameter-declaration programs "
          f"identical to legacy and to the oracle; {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
