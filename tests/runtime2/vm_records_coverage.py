#!/usr/bin/env python3
"""records20k coverage (integration stage 4, coordinator ruling): the two
CP12 refusals lifted with their safety conditions, as differential suites.

  SUPPORTED   shapes now compiled, each run under NIFT_VM=require and compared
              with legacy byte for byte (stdout, stderr, exit status):
              * a loop call to a function legacy cannot prepare (bare
                `return`; `?:`/`??` in a `return` or `while` condition) whose
                arguments are literals or name reads:
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
import re
import shutil
import subprocess
import sys
import tempfile

SUPPORTED = [
    ('push_arrlit_fnloop', 'fn(add(a, i)) { a.push([i, i + 1]) \nreturn a.length() }\na := []\ni := 0\nwhile (i < 2) { add(a, i); i += 1 }\nprint(a.stringify())\n'),
    ('push_call_loop', 'fn(mk(i)) { return {"id": i, "t": [i]} }\na := []\ni := 0\nwhile (i < 3) { a.push(mk(i)); i += 1 }\nprint(a.stringify())\nprint(a[2].t[0] + a[1].id)\n'),
    ('push_call_top', 'fn(mk(i)) { return {"id": i, "t": [i]} }\na := []\na.push(mk(1))\na.push(mk(2))\nprint(a.stringify())\n'),
    ('push_objlit_loop', 'a := []\ni := 0\nwhile (i < 3) { a.push({"k": i, "s": "v" + i, "l": [i, i]}); i += 1 }\nprint(a.stringify())\n'),
    ('push_records_shape', 'fn(make_record(i)) {\n  return {"id": i, "name": "item" + i, "score": (i * 37) % 101, "tags": [i % 3, i % 5]}\n}\nrecords := []\ni := 0\nwhile (i < 4) {\n  records.push(make_record(i))\n  i += 1\n}\nprint(records.stringify())\nprint(records[3].tags[1] + records[2].score)\n'),
    ('push_ternary_lit', 'a := []\ni := 0\nwhile (i < 2) { a.push((i > 0 ? [1] : [2])); i += 1 }\nprint(a.stringify())\n'),
    ('push_then_mutate', 'fn(mk(i)) { return [i] }\na := []\nv := mk(5)\na.push(mk(1))\ni := 0\nwhile (i < 1) { a.push(mk(2)); i += 1 }\na.push(mk(3))\nn := a.length()\nprint(n)\nprint(a.stringify())\n'),
    # No `if` shape here (stage4e review): every `if` condition legacy's
    # prepared parser rejects is either refused (a comparison chain:
    # chain-if-condition below) or not compiled at all (`?:`, `??` and
    # literals in an `if` header are opaque:irregular-legacy-parse); the
    # conditions the VM compiles (`!(p < 2)`, `p % 2 == 1`) are preparable.
    ('up_bare_return', 'fn(f(p)) { if (p > 0) { print("pos")\nreturn }\nprint("zero") }\ni := 0\nwhile (i < 2) { f(i); i += 1 }\n'),
    ('up_coalesce', 'fn(f(p)) { return (p ?? 1) }\ni := 0\nwhile (i < 2) { print(f(i)); i += 1 }\n'),
    ('up_fn_from_loop_fn', 'fn(f(p)) { return (true ? p * 2 : 0) }\nfn(g(q)) { return f(q) + 1 }\ni := 0\nwhile (i < 2) { print(g(i)); i += 1 }\n'),
    ('up_neg_lit', 'fn(f(p)) { return (true ? p : 0) }\ni := 0\nwhile (i < 1) { print(f(-3)); i += 1 }\n'),
    ('up_object_str', 'fn(mk(i)) { return {"id": i, "name": "item" + i} }\ni := 0\ns := ""\nwhile (i < 3) { o := mk(i); s += o.name; i += 1 }\nprint(s)\n'),
    ('up_string_lit', 'fn(f(p)) { return (true ? p : "z") }\ni := 0\nwhile (i < 1) { print(f("ab")); i += 1 }\n'),
    ('up_ternary_lit', 'fn(f(p)) { print("body " + p)\nreturn (true ? p : 0) }\ni := 0\nwhile (i < 2) { x := f(1); print(x); i += 1 }\n'),
    ('up_ternary_local', 'fn(f(p)) { print("body " + p)\nreturn (true ? p : 0) }\ni := 0\nwhile (i < 2) { x := f(i); print(x); i += 1 }\n'),
    ('up_while_cond', 'fn(f(p)) { return (p > 0 ? 1 : 0) }\ni := 0\nwhile (i < 3 && f(i) < 5) { print(i); i += 1 }\n'),
]

# A member or index path as a user-call argument (coordinator ruling, VM
# subset): admitted when inference proves its kinds certainly scalar. Reads
# (the records shape, rooted at a parameter, through another call's
# arguments), evaluation once and in order, the binding type of a copy, then
# failures in legacy's location wording (the whole argument text at its
# start) at top level, in loops and in a function; the existing wording
# elsewhere (`print(o.zz)`, a declaration in a loop) is unchanged. Run under
# NIFT_VM=require, identical to legacy byte for byte.
PATH_ARGUMENTS = [
    ('pa_records_shape', 'fn(make_record(i)) {\n  return {"id": i, "name": "item" + i, "score": (i * 37) % 101}\n'
     '}\nfn(pad(s, width)) {\n  out := s\n  while (out.length() < width) {\n    out += "."\n  }\n  return out\n}\n'
     'records := []\ni := 0\nwhile (i < 4) {\n  records.push(make_record(i))\n  i += 1\n}\nreport := ""\nk := 0\n'
     'while (k < 4) {\n  report += pad(records[k].name, 7) + ";"\n  k += 1\n}\nprint(report)\n'
     'print(pad(records[0].name, 2))\n'),
    ('pa_param_root_loop', 'fn(f(s)) {\n  return s + "!"\n}\nfn(g(o)) {\n  return f(o.k)\n}\nr := {"k": "a"}\n'
     'i := 0\nwhile (i < 2) {\n  print(g(r))\n  i += 1\n}\n'),
    ('pa_nested', 'fn(f(s)) {\n  return s + "!"\n}\nfn(g(t)) {\n  return "<" + t + ">"\n}\no := {"k": "a"}\n'
     'i := 0\nwhile (i < 2) {\n  print(f(g(o.k)))\n  i += 1\n}\nprint(f(g(o.k)))\n'),
    ('pa_once_order', 'fn(g()) {\n  print("g")\n  return 1\n}\nfn(h()) {\n  print("h")\n  return 2\n}\n'
     'fn(f(s, t)) {\n  return s + t\n}\na := [5, 7]\nprint(f(a[g()], h()))\nprint(f(h(), a[g()]))\ni := 0\n'
     'while (i < 1) {\n  print(f(a[g()], a[0]))\n  i += 1\n}\n'),
    ('pa_type_register', 'fn(f(s)) {\n  out := s\n  print(out)\n  out = "x"\n  print(out)\n  return 0\n}\n'
     'o := {"k": "a", "n": 1}\nf(o.k)\nf(o.n)\n'),
    ('pa_double_member', 'fn(f(s)) {\n  return s * 2\n}\no := {"x": 1.25}\na := [0.5, 2.5]\nprint(f(o.x))\ni := 0\n'
     'while (i < 2) {\n  print(f(a[i]))\n  i += 1\n}\n'),
    ('pe_missing_top', 'fn(f(s)) {\n  return s\n}\na := [{"k": 1}, {"j": 2}]\nj := 1\nprint(f(a[0].k))\n'
     'print(f(a[j].k))\n'),
    ('pe_missing_loop', 'fn(f(s)) {\n  return s\n}\na := [{"k": 1}, {"j": 2}]\ni := 0\nwhile (i < 2) {\n'
     '  print(f(a[i].k))\n  i += 1\n}\n'),
    ('pe_missing_mid_loop', 'fn(f(s)) {\n  return s\n}\na := [{"k": {"b": 1}}, {"j": 2}]\ni := 0\nwhile (i < 2) {\n'
     '  print(f(a[i].k.b))\n  i += 1\n}\n'),
    ('pe_missing_fn', 'fn(f(s)) {\n  return s\n}\nfn(mk(i)) {\n  if (i == 0) {\n    return {"k": 1}\n  }\n'
     '  return {"j": 2}\n}\nfn(g(r)) {\n  v := f(r.k)\n  return v\n}\ni := 0\nwhile (i < 2) {\n  print(g(mk(i)))\n'
     '  i += 1\n}\n'),
    ('pe_oob_mid_loop', 'fn(f(s)) {\n  return s\n}\na := [{"y": 1}]\ni := 0\nwhile (i < 3) {\n'
     '  print(f(a[i * 5].y))\n  i += 1\n}\n'),
    ('pe_oob_top', 'fn(f(s)) {\n  return s\n}\na := [{"y": 1}]\nj := 5\nprint(f(a[j].y))\n'),
    ('pe_missing_key', 'fn(f(s)) {\n  return s\n}\no := {"k": 1}\nk := "zz"\nprint(f(o[k]))\n'),
    ('pe_spaces_ok', 'fn(f(s)) {\n  return s\n}\na := [{"k": 1}, {"j": 2}]\nj := 1\nprint(f( a[j].k ))\n'),
    ('pe_second_arg', 'fn(f(s, t)) {\n  return t\n}\na := [{"k": 1}, {"j": 2}]\nj := 1\nprint(f(1, a[j].k))\n'),
    ('pe_unchanged_print', 'o := {"k": "a"}\nprint(o.zz)\n'),
    ('pe_unchanged_decl_loop', 'a := [{"k": 1}, {"j": 2}]\ni := 0\nwhile (i < 2) {\n  v := a[i].k\n  print(v)\n'
     '  i += 1\n}\n'),
]

# The same failure where legacy reports a `return` statement: the approved
# D-018c difference (legacy's `return: ` prefix and the statement's position);
# stdout, exit status and the message must otherwise be identical.
PATH_ARGUMENTS_D018C = [
    ('pe_missing_fn_return', 'fn(f(s)) {\n  return s\n}\nfn(mk(i)) {\n  if (i == 0) {\n    return {"k": 1}\n  }\n'
     '  return {"j": 2}\n}\nfn(g(r)) {\n  return f(r.k)\n}\nprint(g(mk(0)))\nprint(g(mk(1)))\n'),
]

PARAM_DECL = [
    # (name, must run on the VM, source); the oracle is ORACLE[name]
    # Arguments that may be aggregates (vals[i]: path-in-call-argument, legacy
    # binds a location) or double-typed text (prepared typing differs):
    # refused, identical to legacy (stage4d corrected these). pd_pad's
    # argument names[k] is a certainly-string path, admitted since the path
    # argument ruling: now required on the VM.
    ("pd_types_loop", False, 'fn(f(s)) {\n  out := s\n  print(out.stringify())\n  return 0\n}\n'
     'vals := ["a", 2, 2.5, [1, "b"], {"k": 1}]\ni := 0\nwhile (i < 5) { f(vals[i]); i += 1 }\nf("top")\nf(7)\n'),
    ("pd_types_literals", False, 'fn(f(s)) {\n  out := s\n  print(out.stringify())\n  return 0\n}\ni := 0\n'
     'while (i < 1) { f("a"); f(2); f(2.5); f([1, 2]); f({"k": 1}); i += 1 }\n'),
    ("pd_pad", True, 'fn(pad(s, width)) {\n  out := s\n  while (out.length() < width) { out += "." }\n  return out\n}\n'
     'names := ["item0", "it", "x"]\nk := 0\nr := ""\nwhile (k < 3) { r += pad(names[k], 6) + ";"; k += 1 }\n'
     'print(r)\nprint(pad("top", 5))\n'),
    # On the VM: string, int and untyped (call result) arguments, loops and top level.
    ("pd_scalars_loop", True, 'fn(dbl()) { return 2.5 }\nfn(f(s)) {\n  out := s\n  print(out.stringify())\n  return 0\n}\n'
     'i := 0\nwhile (i < 1) { f("a"); f(2); f(dbl()); i += 1 }\nf("top")\nf(7)\n'),
    ("pd_pad_calls", True, 'fn(nm(k)) {\n  if (k == 0) { return "item0" }\n  if (k == 1) { return "it" }\n  return "x"\n}\n'
     'fn(pad(s, width)) {\n  out := s\n  while (out.length() < width) { out += "." }\n  return out\n}\n'
     'k := 0\nr := ""\nwhile (k < 3) { r += pad(nm(k), 6) + ";"; k += 1 }\nprint(r)\nprint(pad("top", 5))\n'),
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
    # The generator's prepared-decl-fn shape with an int argument text (stage4e
    # review: admitted by the rule, legacy types the copy from the value):
    # in a loop, in a function's loop, that function called from a loop, in
    # an `if` block's loop.
    ("pd_int_text_loop", True, 'fn(zg(p)) {\n  zq := p\n  return zq\n}\nzi := 0\nwhile (zi < 1) {\n  zi += 1\n'
     '  print(zg(1))\n}\n'),
    ("pd_int_text_fn", True, 'fn(zg(p)) {\n  zq := p\n  return zq\n}\nfn(zw()) {\n  zi := 0\n  while (zi < 1) {\n'
     '    zi += 1\n    print(zg(1))\n  }\n  return 0\n}\nzw()\n'),
    ("pd_int_text_fnloop", True, 'fn(zg(p)) {\n  zq := p\n  return zq\n}\nfn(zw()) {\n  zi := 0\n'
     '  while (zi < 1) {\n    zi += 1\n    print(zg(1))\n  }\n  return 0\n}\nzl := 0\nwhile (zl < 1) {\n'
     '  zl += 1\n  zw()\n}\n'),
    ("pd_int_text_if", True, 'fn(zg(p)) {\n  zq := p\n  return zq\n}\nif (true) {\n  zi := 0\n  while (zi < 1) {\n'
     '    zi += 1\n    print(zg(1))\n  }\n}\n'),
    ("pd_reassign_top_only", True, 'fn(f(s)) {\n  out := s\n  out = 1.5\n  print(out)\n  return 0\n}\nf(2.5)\nf("a")\n'),
]

# Outer bindings of the copied parameter's name (coordinator ruling, VM
# subset): legacy types `out := s` from a binding `s` in the caller's scopes
# when preparing; an int-typed one types it like none, from the value. The 36
# probes (outer binding x argument x reassignment) and five integral-value
# ones: admitted (on the VM, or refused only where the reassignment can fail
# in loop code, context:assignment-error, as for any loop assignment) when
# every outer `s` is int text or untyped text holding a certainly integral
# number; refused (prepared-decl-type) for a string or double outer binding of
# any origin and for an untyped one whose kinds are not certainly int.
OUTER_FN = 'fn(f(s)) {{\n  out := s\n  out = {r}\n  print(out)\n  return 0\n}}\n'
OUTER_LOOP = 'i := 0\nwhile (i < 1) {{\n  f({a})\n  i += 1\n}}\n'
OUTER_BINDINGS = {"none": ("", "admitted"), "str": ('s := "x"\n', "string"), "dbl": ('s := 2.5\n', "double"),
                  "int": ('s := 7\n', "admitted"), "untyped": ('v := [7]\ns := v[0]\n', "admitted"),
                  "untyped_str": ('v := ["x"]\ns := v[0]\n', "string")}
OUTER_VALUES = {"int": "7", "str": '"a"', "dbl": "1.5"}
# Untyped outer declarations holding integral numbers (legacy: int, value
# typing) or not certainly integral: argument "a", reassignment "b".
OUTER_INTEGRAL = [("e2", 'v := [1e2]\ns := v[0]\n', "vm"), ("pt0", 'v := [2.0]\ns := v[0]\n', "vm"),
                  ("mul", 'v := [1e2]\ns := v[0] * 1\n', "vm"), ("neg", 'v := [-5]\ns := v[0]\n', "vm"),
                  ("half", 'v := [2.5]\ns := v[0]\n', "prepared-decl-type"),
                  ("lit_e2", 's := 1e2\n', "prepared-decl-type")]


def outer_cases():
    """(name, source, expected outcome: "vm" or the refusal reason)."""
    out = []
    for g, (decl, typing) in OUTER_BINDINGS.items():
        for a in ("int", "str"):
            for r in ("int", "str", "dbl"):
                source = OUTER_FN.format(r=OUTER_VALUES[r]) + decl + OUTER_LOOP.format(a=OUTER_VALUES[a])
                if typing != "admitted":
                    want = "prepared-decl-type"
                else:  # out takes the argument's type; an int accepts nothing else here, a string neither
                    want = "vm" if r == a else "context:assignment-error"
                out.append((f"outer_{g}_{a}_{r}", source, want))
    for g, decl, want in OUTER_INTEGRAL:
        out.append((f"outer_integral_{g}", OUTER_FN.format(r='"b"') + decl + OUTER_LOOP.format(a='"a"'), want))
    return out


# Known legacy inconsistencies: name -> (register entry, legacy's actual
# printed lines, legacy ends with an error). The VM must refuse these programs.
LEGACY_INCONSISTENT = {
    'pd_register_type_loop': ('L52', ['100'], True),
}

# name -> (printed lines, a binding-type error ends the run), derived from the
# arguments by the rule above (pd_pad: s padded with "." to the width).
ORACLE = {
    'pd_types_loop': (['"a"', '2', '2.5', '[1,"b"]', '{"k":1}', '"top"', '7'], False),
    'pd_scalars_loop': (['"a"', '2', '2.5', '"top"', '7'], False),
    'pd_pad_calls': (['item0.;it....;x.....;', 'top..'], False),
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
    'pd_int_text_loop': (['1'], False),
    'pd_int_text_fn': (['1'], False),
    'pd_int_text_fnloop': (['1'], False),
    'pd_int_text_if': (['1'], False),
}

REFUSED = [
    # Integration review (stage4d): these were listed as supported, but each
    # contains a construct that is refused for a reason that still holds: a
    # pushed member of a binding and a declaration from an array element are
    # places (aliasing, L7: legacy binds locations); `p < 2 == true` is a
    # comparison chain (CP12 F2: legacy splits chains differently). Kept here
    # with that reason; the supported list has the corrected programs.
    ("push-member-after-call", 'fn(mk(i)) { return {"id": i, "t": [i]} }\na := []\na.push(mk(1))\nb := mk(2)\na.push(b.t)\nprint(a.stringify())\n', "aliasing"),
    ("decl-from-pushed-element", 'fn(mk(i)) { return [i] }\na := []\nv := mk(5)\na.push(mk(1))\ni := 0\nwhile (i < 1) { a.push(mk(2)); i += 1 }\nx := a[0]\nprint(a.stringify())\n', "aliasing"),
    ("chain-if-condition", 'fn(f(p)) { if (p < 2 == true) { print("lt") }\nreturn p }\ni := 0\nwhile (i < 3) { print(f(i)); i += 1 }\n', "comparison-chain"),
    # The parameter copy where legacy's prepared typing differs from the
    # declared type: a binding of the same name outside the function
    # (review2_m1_param_caller_scope), a double-typed argument text.
    ("param-copy-outer-name", 'fn(f(a)) {\n  b := a\n  b = 1.5\n  return b\n}\na := "s"\ni := 0\n'
     'while (i < 1) { print(f(2.5)); i += 1 }\n', "prepared-decl-type"),
    ("param-copy-double-text", 'fn(f(s)) {\n  out := s\n  print(out)\n  return 0\n}\ni := 0\n'
     'while (i < 1) { f(2.5); i += 1 }\n', "prepared-decl-type"),
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
    # Path arguments that stay refused (coordinator ruling): an aggregate is
    # shared with the caller in legacy (the callee pushes, a returned copy is
    # pushed, a local copy is pushed; a written scalar parameter is refused as
    # before, alias-argument); a kind that is not certainly scalar: varying
    # across iterations, the root reassigned, mixed pushes, a member present
    # only in one branch, Unknown (every source of Unknown is itself refused,
    # here bytes); an atomic handle stored in a member; a path wrapped in
    # parentheses or inside an expression (legacy's text differs); member on
    # a non-object and index on a string (the path's kinds are empty). A unit
    # with import or export is refused as a whole, which the admission relies
    # on (no write outside the unit changes a kind).
    ('path-push-callee', 'fn(f(s)) {\n  s.push(9)\n  return 0\n}\no := {"k": [1]}\nf(o.k)\n'
     'print(o.k.stringify())\n', 'alias-argument'),
    ('path-return-push', 'fn(f(s)) {\n  return s\n}\no := {"k": [1]}\nr := f(o.k)\nr.push(2)\n'
     'print(o.k.stringify())\n', 'path-in-call-argument'),
    ('path-copy-push', 'fn(f(s)) {\n  t := s\n  t.push(2)\n  return 0\n}\no := {"k": [1]}\nf(o.k)\n'
     'print(o.k.stringify())\n', 'path-in-call-argument'),
    ('path-unknown-kind', 'fn(f(s)) {\n  return 0\n}\no := {"k": bytes("ab")}\nf(o.k)\n', 'builtin-bytes:bytes'),
    ('path-mixed-iter', 'fn(f(s)) {\n  return 0\n}\na := [1, [2]]\ni := 0\nwhile (i < 2) {\n  f(a[i])\n  i += 1\n}\n',
     'path-in-call-argument'),
    ('path-root-reassigned', 'fn(f(s)) {\n  return 0\n}\no := {"k": 1}\nf(o.k)\no = {"k": [1]}\nf(o.k)\n',
     'path-in-call-argument'),
    ('path-mixed-push', 'fn(f(s)) {\n  return 0\n}\na := []\na.push(1)\na.push([2])\ni := 0\nwhile (i < 2) {\n'
     '  f(a[i])\n  i += 1\n}\n', 'path-in-call-argument'),
    ('path-member-branch', 'fn(f(s)) {\n  return 0\n}\no := {"j": 1}\ni := 0\nwhile (i < 2) {\n  if (i > 0) {\n'
     '    o = {"k": [1]}\n  }\n  f(o.k)\n  i += 1\n}\n', 'path-in-call-argument'),
    ('path-handle', 'fn(f(s)) {\n  s += 1\n  return 0\n}\nc := atomic<int>(5)\no := {"h": c}\nf(o.h)\nprint(c)\n',
     'callable:atomic<int>'),
    ('path-import', 'fn(f(s)) {\n  return s\n}\nimport("nope.f")\no := {"k": 1}\nprint(f(o.k))\n', 'statement:Import'),
    ('path-export', 'fn(f(s)) {\n  return s\n}\no := {"k": 1}\nprint(f(o.k))\nexport(o)\n', 'statement:Export'),
    ('path-member-on-int', 'fn(f(s)) {\n  return s\n}\no := {"a": 1}\nprint(f(o.a.b))\n', 'path-in-call-argument'),
    ('path-index-on-string', 'fn(f(s)) {\n  return s\n}\ns := "abc"\nprint(f(s[1]))\n', 'path-in-call-argument'),
    ('path-paren', 'fn(f(s)) {\n  return s\n}\na := [{"k": 1}, {"j": 2}]\nj := 1\nprint(f((a[j].k)))\n',
     'path-in-call-argument'),
    ('path-expression', 'fn(f(s)) {\n  return s\n}\na := [{"k": 1}, {"j": 2}]\nj := 1\nprint(f(a[j].k + 1))\n',
     'path-in-call-argument'),
    ('path-write-scalar', 'fn(f(s)) {\n  s = 9\n  return s\n}\no := {"k": 1}\nprint(f(o.k))\nprint(o.k)\n',
     'alias-argument'),
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


def d018c_equal(legacy, vm):
    """D-018c: one diagnostic each; equal once legacy's `return: ` prefix and
    both positions are removed (the VM reports the failing expression)."""
    def strip(err):
        lines = err.decode("utf-8", "replace").splitlines()
        return [re.sub(r"^error: .*?:\d+:\d+: (return: )?", "error: ", line) for line in lines]
    return (legacy[0] == vm[0] and legacy[2] == vm[2] != 0 and len(strip(legacy[1])) == 1
            and strip(legacy[1]) == strip(vm[1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--timeout", type=float, default=60)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    failures = []
    work = tempfile.mkdtemp(prefix="nift-vm-records-")
    try:
        supported = [(n, s, None) for n, s in SUPPORTED + PATH_ARGUMENTS]
        d018c = [(n, s, "d018c") for n, s in PATH_ARGUMENTS_D018C]
        for kind, cases in (("supported", supported + d018c), ("refused", REFUSED)):
            for name, source, reason in cases:
                d = tempfile.mkdtemp(dir=work)
                with open(os.path.join(d, "p.f"), "w") as f:
                    f.write(source)
                legacy, _ = run(nift, d, None, a.timeout)
                if kind == "supported":
                    vm, st = run(nift, d, "require", a.timeout)
                    if not st or st["outcome"] not in ("vm", "vm-error"):
                        failures.append(f"{name}: not on the VM ({st and (st['outcome'], st['reason'])})")
                    elif reason == "d018c" and not d018c_equal(legacy, vm):
                        failures.append(f"{name}: differs beyond D-018c\n  legacy {legacy}\n  vm     {vm}")
                    elif reason != "d018c" and vm != legacy:
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
        for name, source, want in outer_cases():
            d = tempfile.mkdtemp(dir=work)
            with open(os.path.join(d, "p.f"), "w") as f:
                f.write(source)
            legacy, _ = run(nift, d, None, a.timeout)
            auto, st = run(nift, d, "1", a.timeout)
            _, cst = run(nift, d, "classify", a.timeout)
            got = "vm" if cst and cst["outcome"] == "compiled" else cst and cst["reason"]
            if got != want:
                failures.append(f"{name}: expected {want}, got {cst and (cst['outcome'], cst['reason'])}")
            elif auto != legacy:
                failures.append(f"{name}: NIFT_VM=1 output differs\n  legacy {legacy}\n  auto   {auto}")
            elif want == "vm" and (not st or st.get("outcome") != "vm"):
                failures.append(f"{name}: not on the VM ({st and st.get('outcome')})")
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
    print(("PASS" if not failures else "FAIL") + f" vm records coverage: {len(SUPPORTED)} supported shapes and "
          f"{len(PATH_ARGUMENTS) + len(PATH_ARGUMENTS_D018C)} path-argument programs on the VM (identical to "
          f"legacy, D-018c for {len(PATH_ARGUMENTS_D018C)}), {len(REFUSED)} refused, {len(PARAM_DECL)} "
          f"parameter-declaration programs and {len(outer_cases())} outer-binding programs "
          f"identical to legacy and to the oracle; {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
