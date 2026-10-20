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
    # CP15 (batch B): numbers, sha256 of strings, validate/copy/deepcopy on
    # plain values, `{...}` literals read as JSON (W9: JSON escapes, -0 and
    # exact big integers are JSON's, not the Nift literal evaluator's).
    "f15-numbers": (
        "x := -2.5\nprint(x.abs())\nprint(x.floor())\nprint(x.ceil())\nprint(x.round())\n"
        "y := 2.5\nprint(y.round())\nz := 7\nprint(z.to_int())\nw := 3.0\nprint(w.to_int())\n"
        "neg := 0 - 7\nprint(neg.abs())\nfr := 0.1 * 3\nprint(fr.round())\n"
    ),
    "f15-sha-validate-copy": (
        'print(sha256("abc"))\nprint(sha256(""))\n'
        'schema := {"type": "object", "required": ["id"], "properties": {"id": {"type": "integer"}}}\n'
        'rec := {"id": 7, "tags": ["a"]}\nok := validate(schema, rec)\nprint(ok.stringify())\n'
        "arr := [1, [2]]\nc := copy(arr)\nc.push(3)\nprint(arr.stringify())\nprint(c.stringify())\nc2 := copy(rec)\nprint(c2.stringify())\n"
        'd := deepcopy(rec)\nprint(d.stringify())\nn := 5\nm := copy(n)\nprint(m)\n'
    ),
    "f15-json-literals": (
        'j := {"z": -0, "big": 12345678901234567890123, "f": 0.10, "e": 1e300}\n'
        "print(j.stringify())\nprint(j.z)\nprint(j.big)\nprint(j.f)\nprint(j.e)\n"
        'nested := {"a": [1, {"b": null}], "t": true}\nprint(nested.stringify())\nprint(nested.t)\n'
        "i := 3\nmixed := {\"k\": i, \"l\": [i, 2]}\nprint(mixed.stringify())\n"
    ),
    # CP15 W9 (option A): a `{...}` literal that is JSON is read as JSON
    # (LoadK); any other is compiled as a Nift literal, legacy's fallback.
    # Nested at least two levels, in arrays and objects; JSON and fallback
    # spellings of the same values must print identically (pj/pf); a JSON
    # object inside a fallback literal is still read as JSON (`-0.0`,
    # `1e-400` kept; the integral `-0` is in SUPPORTED_TOP, as legacy reads it
    # as 0 in loop code).
    "w9-literals": (
        'o0 := {"a": .5}\nprint(o0.a)\no1 := {"a": 1, "b": .5}\nprint(o1.stringify())\n'
        'o4 := {"a": {"b": -0.0}, "c": .5}\nprint(o4.stringify())\na6 := [{"b": 1e-400}, .5]\nprint(a6.stringify())\n'
        "o7 := {\"s\": 'q', \"t\": {\"u\": [1., .5, 'r']}}\nprint(o7.stringify())\nprint(o7.s)\n"
        'o9 := {"a": {"b": 0.10, "c": [1, {"d": -0.00}]}, "e": .5}\nprint(o9.stringify())\n'
        'o10 := {"b": -0.0}\nprint(o10.stringify())\nv10 := o10.b\nprint(v10)\nprint("b=" + v10)\n'
        # `-0` with a string-valued sibling: legacy's prepared parser rejects
        # the literal, so legacy reads it as JSON in every context (-0 kept).
        'ns := {"b": -0, "c": "s"}\nprint(ns.stringify())\nprint(ns.b)\n'
        'nn := {"a": {"b": -0e0, "s": "x"}, "t": "y"}\nprint(nn.stringify())\n'
        'pj := {"h": 0.5, "k": 2500, "m": 0.25, "n": 1e-7, "p": 0.1, "s": "q", "t": [1, 0.5, "r"], "u": {"v": 1, "w": 0.5}}\n'
        "pf := {\"h\": .5, \"k\": 2.5E+3, \"m\": 2.5e-1, \"n\": 1e-7, \"p\": 0.1, \"s\": 'q', \"t\": [1., .5, 'r'], "
        '"u": {"v": 1., "w": .5}}\n'
        "print(pj.stringify())\nprint(pf.stringify())\nprint(pj.stringify() == pf.stringify())\n"
        "print(pj.h)\nprint(pf.h)\nprint(pj.k)\nprint(pf.k)\nprint(pj.n)\nprint(pf.n)\n"
        'dj := {"a": 1e300, "b": 1.5e-7, "c": 1e21, "d": 123456789012.5, "e": 5e-324, "f": 1.7976931348623157e308, '
        '"g": -2.5e-300, "h": 1e-400, "i": 9007199254740993, "j": 1.50, "k": 0.0}\n'
        "print(dj.stringify())\nprint(dj.c)\nprint(dj.e)\nprint(dj.i)\n"
    ),
    # Sign of zero vs the `-0` spelling (owner, CP15 record "Negative zero"):
    # the sign of zero is never observable as a number (arithmetic results
    # print canonically, `x == 0`); a JSON literal's `-0`/`-0.0` keeps its
    # SPELLING through storage, member/index access, nesting, print,
    # stringify and concatenation; arithmetic on it gives the canonical form.
    "negzero-sign": (
        'o := {"b": -0, "e": -0.0, "s": "x", "c": [-0, {"d": -0, "s": "y"}]}\nprint(o.stringify())\nprint(o.b)\n'
        'print(o.e)\nprint("s" + o.b)\nprint("s" + o.e)\nprint(o.c.stringify())\nprint(o.c[0])\nprint(o.c[1].d)\n'
        'v := o.b\nw := o.e\nprint(v + 0)\nprint(v - 0)\nprint(v * 1)\nprint(v * -1)\nprint(w + 0)\nprint(w * 1)\n'
        'print(v == 0)\nprint(w == 0)\nprint(v < 0)\nprint(v > -1)\n'
        'q := {"b": 0, "e": 0, "s": "x", "c": [0, {"d": 0, "s": "y"}]}\nprint(o == q)\n'
        'x := 0.5 - 0.5\nprint(x * -1)\ny := -0.5 * 0\nprint(y)\nprint("s" + y)\na := [y]\nprint(a.stringify())\n'
        'print(y == 0)\nprint(y + 0)\ni := 0\nprint(i * -1)\njj := {"c": [-0, 1], "s": "x"}\nprint(jj.c.join(","))\n'
    ),
}

# Supported at top level only: `-0` read as Nift (a fallback literal or an
# array element) is integer negation, refused in loop code
# (context:negative-zero, CP12 #11; pinned in UNSUPPORTED).
SUPPORTED_TOP = [
    # The one place the two readings meet: JSON except for one fallback
    # spelling (`.5`). As JSON `-0` stays -0; read as Nift it is 0.
    ("w9-meet", 'jn := {"a": 0.10, "b": -0, "c": 0.5}\nfb := {"a": 0.10, "b": -0, "c": .5}\n'
                "print(jn.stringify())\nprint(fb.stringify())\nprint(jn.b)\nprint(fb.b)\n"),
    ("w9-json-negzero-nested", 'o4 := {"a": {"b": -0}, "c": .5}\nprint(o4.stringify())\na6 := [{"b": -0}, .5]\n'
                               'print(a6.stringify())\no9 := {"a": {"b": 0.10, "c": [1, {"d": -0e0}]}, "e": .5}\n'
                               'print(o9.stringify())\n'),
    ("w9-fallback-array-negzero", 'o5 := {"a": [-0, {"b": -0}], "c": .5}\nprint(o5.stringify())\n'),
    ("w9-fallback-zero-decimal", 'o := {"a": -0.0, "b": .5}\nprint(o.stringify())\n'),
    ("w9-fallback-denormal", 'o := {"a": 5e-324, "b": .5}\nprint(o.stringify())\n'),
    # Negation (unary minus of a possibly integer operand is refused in loop
    # code, CP12 #11): `-(-0)` is 0, `-(-0.0)` is 0.0 (legacy flips the
    # spelling's sign), `-(0.5 - 0.5)` prints 0.
    ("negzero-negation", 'o := {"b": -0, "e": -0.0, "s": "x"}\nv := o.b\nw := o.e\nprint(-v)\nprint(-w)\n'
                         'x := 0.5 - 0.5\nprint(-x)\ni := 0\nprint(-i)\nprint(-0 + 0)\nprint(-0 == 0)\n'),
    ("negzero-nested-sort", 'a := [{"b": 1}, {"b": -0}]\nprint(a.stringify())\no := {"a": {"b": [-0]}}\nprint(o.a.b[0])\n'
                            'print(o.a.stringify())\n'),
    # CP12 text typing of `{..., "m": -1e-400}` meets the digit-led root `1e`:
    # type -1, as legacy (also for a declaration in a function called from a
    # loop, where the declaration's text type must be known statically).
    ("texttype-digit-root", 'fn(h()) {\nd3 := {"m": -1e-400, "n": 3}\nprint(d3.stringify())\nreturn 0\n}\n'
                            'dj := {"a": 1, "m": -1e-400, "s": "x"}\nprint(dj.stringify())\nprint(dj.m)\n'
                            'i := 0\nwhile (i < 1) { i += 1\nd2 := {"m": -1e-400, "n": 2}\nprint(d2.stringify())\nh() }\n'),
    # The -0 finding (open defect on runtime-2 since 4b2fc909): every shape.
    ("negzero-shapes", 'fn(f(v)) { return v.b }\nfn(g()) { return {"b": -0} }\n'
                       'a := [-0]\nprint(a.stringify())\nx := -0\nprint(x)\n'
                       'print(f({"b": -0}))\nr := g()\nprint(r.stringify())\n'
                       'o := {"b": -0}\nv := o.b\nprint(v)\nprint(v + 0)\nprint("s" + v)\n'
                       'print("o=" + o.stringify())\nprint("b=" + o.b)\nq := {"b": 0}\nprint(o == q)\nprint(o.b == 0)\n'),
]

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
    # CP15: N1 errors, sha256 argument kinds, validate rejection (legacy's
    # catchable Error, uncaught here: same output; code schema.rejected).
    ("num-to-int-fraction", "x := 2.5\nprint(x.to_int())\n"),
    ("sha256-kind", "print(sha256(1))\n"),
    ("validate-rejected", 'schema := {"type": "string"}\nv := 5\nr := validate(schema, v)\nprint(r)\n'),
    # Division by any zero is an error, whatever its sign or spelling.
    ("negzero-div-json", 'o := {"b": -0, "s": "x"}\nprint(1 / o.b)\n'),
    ("negzero-div-json-double", 'o := {"b": -0.0}\nprint(1 / o.b)\n'),
    ("negzero-div-arith", "x := -0.5 * 0\nprint(1 / x)\n"),
    ("validate-bad-schema", 'schema := {"type": 5}\nv := 5\nr := validate(schema, v)\nprint(r)\n'),
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
    # Q14 / W6 presentation chain: refused in every context (legacy composes
    # the chain over the original value outside loop code). Since CP12b's
    # chained postfix (cp12b-resolver.md, accepted) a method call on a call
    # result compiles, so the refusal is W6's own reason; before it,
    # postfix-receiver refused first and W6 was defence in depth. The program
    # is refused either way; only the refusing layer changed.
    ("q14-presentation-chain", 'o := {"a": 1}\nprint(o.stringify().stringify())\n', "builtin-presentation-chain"),
    # F4 mutating methods and F5 callbacks: not wired.
    ("f4-pop", "a := [1]\nx := a.pop()\nprint(x)\n", "method:pop"),
    ("f4-sort", "a := [2, 1]\nb := a.sort()\nprint(b.size())\n", "method:sort"),
    # Batch B (CP15) and C (CP16) names.
    # CP15 W8: bytes have no CP12 kind bit; W9: a literal of constants that
    # is not JSON; W10: a quoted-literal validate argument (J-Q2).
    ("w8-bytes", "b := bytes([1, 2])\nprint(1)\n", "builtin-bytes:bytes"),
    ("w8-encode", 's := "a"\nb := s.encode("utf-8")\nprint(1)\n', "builtin-bytes:encode"),
    # W9 option A: a literal that is not JSON is compiled as a Nift literal,
    # so the CP10/CP11 guards refuse exactly what they refused before.
    ("w9-fallback-leading-zero", 'o := {"z": 01, "a": 1}\nprint(o.stringify())\n', "number-literal"),
    ("w9-fallback-non-finite", 'o := {"z": 1e400, "a": 1}\nprint(o.stringify())\n', "number-literal"),
    ("w9-fallback-int-range", 'o := {"a": 12345678901234567890123, "b": .5}\nprint(o.stringify())\n', "number-literal"),
    ("w9-fallback-escape", 'o := {"z": "\\xff", "a": 1}\nprint(o.stringify())\n', "opaque:legacy-runtime-error"),
    # An integral negative zero in a JSON literal: legacy reads it as 0 in
    # loop code (loop bodies, functions called from loops), -0 elsewhere.
    ("w9-json-negzero-loop", 'i := 0\nwhile (i < 1) { i += 1\no := {"b": -0}\nprint(o.stringify()) }\n',
     "context:negative-zero"),
    ("w9-json-negzero-fnloop", 'fn(h()) { return {"b": {"c": -0e-5}} }\ni := 0\n'
                               'while (i < 1) { i += 1\nr := h()\nprint(r.stringify()) }\n', "context:negative-zero"),
    ("w9-fallback-negzero-loop", 'i := 0\nwhile (i < 1) { i += 1\no := {"b": -0, "c": .5}\nprint(o.b) }\n',
     "context:negative-zero"),
    ("w10-validate-literal", 'schema := {"type": "string"}\nr := validate(schema, "x")\nprint(r)\n',
     "builtin-literal-arg:validate"),
    # W9 cannot see an escaped string inside a structured literal: the CP10
    # parser keeps any such statement Opaque (J-Q1: `\u00e9` means different
    # values in `{...}` and `[...]`), so it runs on legacy before W9 applies.
    ("w9-escaped-string", 'je := {"s": "caf\\u00e9"}\nprint(je.s)\n', "opaque:legacy-runtime-error"),
    ("n-q2-abs-arity", "x := -2\nprint(x.abs(1))\n", "builtin-arity:abs"),
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
# CP15 review R2 (deliberate change from legacy, which recurses without a
# bound): a value nested deeper than 512 levels given to validate stops with
# the fatal limit.exceeded error (exit 1), on the VM only (legacy is not the
# oracle for this case).
LIMIT = ('fn(deep(n)) {\n  if (n == 0) { return [1] }\n  return [deep(n - 1)]\n}\nv := deep(600)\nschema := {"type": "array"}\n'
         'r := validate(schema, v)\nprint(r.size())\n')


def limit_check(nift, work, timeout):
    d = tempfile.mkdtemp(dir=work)
    try:
        with open(os.path.join(d, "p.f"), "w") as f:
            f.write(LIMIT)
        (out, err, rc), st = run(nift, d, "require", timeout)
        if rc != 1 or b"validate: value nesting exceeds 512 levels" not in err or not st or \
                st.get("outcome") != "vm-error" or st.get("code") != "limit.exceeded" or st.get("catchable"):
            return [f"validate-limit: expected exit 1, limit.exceeded (not catchable), got {rc} {err[-160:]!r} {st}"]
        return []
    finally:
        shutil.rmtree(d, ignore_errors=True)


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


# Stage 4 D2 ruling (VM subset; register L56): in textual code (top level,
# `if` blocks, functions not called from loops) legacy swallows a fault of a
# checked method argument and reports the method's type message, in loop code
# the fault itself; a checked argument that can fault (`q.split(h.sep)`: a
# member read) is refused there. The same bodies stay supported in loop code.
REFUSED_IN_CONTEXT = {("shapes", "top"): "builtin-arg-fault-context", ("shapes", "if"): "builtin-arg-fault-context",
                      ("shapes", "fn-top"): "builtin-arg-fault-context"}


def items():
    out = []
    for body_name, body in SUPPORTED.items():
        for ctx, template in CONTEXTS.items():
            indented = "".join("  " + line + "\n" for line in body.splitlines()) if ctx != "top" else body
            reason = REFUSED_IN_CONTEXT.get((body_name, ctx))
            out.append(("unsupported" if reason else "supported", f"{body_name}@{ctx}", template.format(body=indented),
                        reason))
    out += [("supported", n, s, None) for n, s in SUPPORTED_TOP]
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
        failures += limit_check(nift, work, a.timeout)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures:
        print("FAIL " + f)
    count = {k: sum(1 for i in todo if i[0] == k) for k in ("supported", "error", "unsupported")}
    print(("PASS" if not failures else "FAIL") + f" vm built-ins: {count['supported']} supported programs "
          f"({len(SUPPORTED)} bodies x {len(CONTEXTS)} contexts less {len(REFUSED_IN_CONTEXT)} refused there, "
          f"on the VM, identical to legacy), "
          f"{count['error']} error programs, {count['unsupported']} refusals, {len(BUDGET)} budget stops; "
          f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
