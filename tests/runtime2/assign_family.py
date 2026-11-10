#!/usr/bin/env python3
"""Checkpoint 1, the assignment family (docs/runtime-2/checkpoints/combined-b3-b5.md sections 94-95).

  assign_family.py --nift NIFT [--dump]

Every case runs on the VM (NIFT_VM=require) and on legacy (NIFT_VM=0, the same binary). Each is a
script (`.f`) or a session (`.in`, fed to `nift -i`; a session keeps running after an error, so the
target's state after a failed assignment is observable). Both outputs are pinned exactly: stdout,
then `-- exit N`, then the stderr lines that start with `error:` (the case's path removed).

- `legacy: SAME`: legacy's output must equal the VM pin.
- `legacy: (DECISION, PIN)`: an approved difference; legacy must give exactly PIN and the VM its
  own pin. DECISION names the ruling (compatibility.md, "Assignment family").

Every case also fails on an internal failure (exit 70 unless the case pins it, a signal, a
compiler-defect text, a nonzero dispatch-defect counter). All failures are collected and reported.
--dump prints the observed outputs instead (for review; never written to this file).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

INTERNAL = re.compile(r"compiler defect|internal failure|unexpected operands|\x1fnift:|cell reached|"
                      r"AddressSanitizer|LeakSanitizer|runtime error:")
SAME = "SAME"
D2 = "D2 + owner 2026-11-10 16:35 (compound: target read before the rhs)"
D11 = "H-A D1.1 (owner 2026-11-07 22:13: receiver first for every callee)"
B5 = "B5 shared references (Q3 list, owner 2026-11-01/02)"
B5ARG = "B5 shared references: arguments are not copied (Q3 list)"
B5LOOP = "B5 shared references: a loop variable is the element (Q3 list)"
CYCLE = "B5 cycle refusal (runtime.reference_cycle, D-040)"
Q2B = "Q2 B: const is binding-only (owner 2026-11-01/02)"
D018C = "D-018c (D-009#1): one canonical error wording in loop code"


def case(name, src, vm, legacy=SAME, session=False, env=None):
    return {"name": name, "src": src, "vm": vm, "legacy": legacy, "session": session, "env": env or {}}


def refused(line, col, reason, text):
    return ("-- exit 1\nerror: {name}:%d:%d: not compiled for the VM (%s): %s is not supported by the Runtime 2.0 "
            "VM yet\n" % (line, col, reason, text))


CASES = [
    # ---- the three forms, legacy-equal ----
    case("object_member_store", 'o := {"k": 1}\no["k"] = 5\no["n"] = 2\no[\'q\'] = 3\nprint(o.stringify())',
         '{"k":5,"n":2,"q":3}\n-- exit 0\n'),
    case("object_variable_key", 'o := {"a": 1}\nk := "b"\no[k] = 2\nprint(o.stringify())',
         '{"a":1,"b":2}\n-- exit 0\n'),
    case("object_key_order", 'o := {"b": 1, "a": 2}\no["c"] = 3\no["b"] = 9\no["a b"] = 4\no[""] = 5\n'
         'print(o.stringify())', '{"b":9,"a":2,"c":3,"a b":4,"":5}\n-- exit 0\n'),
    case("nested_grid", "grid := [[1, 2], [3, 4]]\ngrid[1][0] = 7\nprint(grid.stringify())",
         "[[1,2],[7,4]]\n-- exit 0\n"),
    case("depth2_object_array", 'o := {"k": [1, 2]}\no["k"][1] = 9\nprint(o.stringify())',
         '{"k":[1,9]}\n-- exit 0\n'),
    case("depth2_array_object", 'a := [{"k": 1}]\na[0]["k"] = 4\na[0]["n"] = 5\nprint(a.stringify())',
         '[{"k":4,"n":5}]\n-- exit 0\n'),
    case("depth3_mixed", 'o := {"a": [{"b": 1}]}\ni := 0\no["a"][i]["b"] = 7\no["a"][0]["c"] = [1]\n'
         'g := [[[1]]]\ng[0][0][0] = 2\nprint(o.stringify())\nprint(g.stringify())',
         '{"a":[{"b":7,"c":[1]}]}\n[[[2]]]\n-- exit 0\n'),
    case("store_as_value", 'o := {"k": 1}\nr := (o["k"] = 5)\nprint(r)\ng := [[0]]\nprint(g[0][0] = 3)\n'
         'print(o.stringify())\nprint(g.stringify())', '5\n3\n{"k":5}\n[[3]]\n-- exit 0\n'),
    # ---- every compound operator, element and member targets ----
    case("compound_array_all_ops", "a := [1, 2, 7, 1, 9]\na[0] += 10\na[1] -= 1\na[2] *= 3\na[3] /= 4\na[4] %= 4\n"
         "print(a.stringify())", "[11,1,21,0.25,1]\n-- exit 0\n"),
    case("compound_member_all_ops", 'o := {"a": 1, "b": 2, "c": 7, "d": 1, "e": 9}\no["a"] += 10\no["b"] -= 1\n'
         'o["c"] *= 3\no["d"] /= 4\no["e"] %= 4\nprint(o.stringify())',
         '{"a":11,"b":1,"c":21,"d":0.25,"e":1}\n-- exit 0\n'),
    case("compound_nested_all_ops", 'g := [[1, 2, 7, 1, 9]]\no := {"m": {"x": 5}}\ng[0][0] += 10\ng[0][1] -= 1\n'
         'g[0][2] *= 3\ng[0][3] /= 4\ng[0][4] %= 4\no["m"]["x"] += 0.5\nk := "m"\no[k]["x"] *= 2\n'
         'print(g.stringify())\nprint(o.stringify())', '[[11,1,21,0.25,1]]\n{"m":{"x":11}}\n-- exit 0\n'),
    case("compound_string_append", 'a := ["x"]\no := {"k": "a"}\ng := [["p"]]\na[0] += "y"\no["k"] += "b"\n'
         'g[0][0] += "q"\nprint(a.stringify())\nprint(o.stringify())\nprint(g.stringify())',
         '["xy"]\n{"k":"ab"}\n[["pq"]]\n-- exit 0\n'),
    case("compound_variable_index", "a := [1, 2]\ni := 1\na[i] += 5\ng := [[1], [2]]\ng[i][0] -= 3\n"
         "print(a.stringify())\nprint(g.stringify())", "[1,7]\n[[1],[-1]]\n-- exit 0\n"),
    # ---- evaluation order and counts ----
    case("order_store_value_then_keys", "g := [[1, 2], [3, 4]]\ni := 0\nf := () => { i = 1; return 5 }\n"
         "g[i][i] = f()\nprint(g.stringify())", "[[1,2],[3,5]]\n-- exit 0\n"),
    case("order_store_object_key", 'o := {"a": 1, "b": 2}\nk := "a"\nf := () => { k = "b"; return 5 }\n'
         'o[k] = f()\nprint(o.stringify())', '{"a":1,"b":5}\n-- exit 0\n'),
    case("order_store_fn_key", "g := [[1, 2], [3, 4]]\ni := 0\nfn(f()) { i = 1; return 5 }\ng[0][i] = f()\n"
         "print(g.stringify())", "[[1,5],[3,4]]\n-- exit 0\n"),
    case("count_rhs_once", "g := [[1]]\no := {\"k\": 1}\ncnt := 0\nf := () => { cnt += 1; return 5 }\n"
         "g[0][0] = f()\ng[0][0] += f()\no[\"k\"] += f()\no[\"n\"] = f()\nprint(cnt)\nprint(g.stringify())\n"
         "print(o.stringify())", '4\n[[10]]\n{"k":6,"n":5}\n-- exit 0\n'),
    case("order_compound_index", "a := [1, 2]\ni := 0\nf := () => { i = 1; return 5 }\na[i] += f()\n"
         "print(a.stringify())\nprint(i)", "[6,2]\n1\n-- exit 0\n", (D2, "[1,6]\n1\n-- exit 0\n")),
    case("order_compound_fn_index", "a := [1, 2]\ni := 0\nfn(f()) { i = 1; return 5 }\na[i] += f()\n"
         "print(a.stringify())", "[6,2]\n-- exit 0\n", (D2, "[1,6]\n-- exit 0\n")),
    case("order_compound_nested_index", "g := [[1, 2], [3, 4]]\ni := 0\nf := () => { i = 1; return 5 }\n"
         "g[i][0] += f()\nprint(g.stringify())", "[[6,2],[3,4]]\n-- exit 0\n",
         (D2, "[[1,2],[6,4]]\n-- exit 0\n")),
    case("order_compound_object_key", 'o := {"a": 1, "b": 10}\nk := "a"\nf := () => { k = "b"; return 5 }\n'
         'o[k] += f()\nprint(o.stringify())', '{"a":6,"b":10}\n-- exit 0\n', (D2, '{"a":1,"b":6}\n-- exit 0\n')),
    case("order_compound_loop", "n := 0\na := [1, 2]\ni := 0\nfn(f()) { i = 1; return 5 }\n"
         "while (n < 1) { a[i] += f(); n += 1 }\nprint(a.stringify())", "[6,2]\n-- exit 0\n",
         (D2, "[1,6]\n-- exit 0\n")),
    case("receiver_rebound_lambda_store", "g := [[1]]\nf := () => { g = [[9]]; return 5 }\nkeep := g\n"
         "g[0][0] = f()\nprint(g.stringify())\nprint(keep.stringify())", "[[9]]\n[[5]]\n-- exit 0\n"),
    case("receiver_rebound_lambda_compound", "g := [[1]]\nf := () => { g = [[9]]; return 5 }\nkeep := g\n"
         "g[0][0] += f()\nprint(g.stringify())\nprint(keep.stringify())", "[[9]]\n[[6]]\n-- exit 0\n"),
    case("receiver_rebound_fn_store", "g := [[1]]\nfn(f()) { g = [[9]]; return 5 }\nkeep := g\ng[0][0] = f()\n"
         "print(g.stringify())\nprint(keep.stringify())", "[[9]]\n[[5]]\n-- exit 0\n",
         (D11, "[[5]]\n[[1]]\n-- exit 0\n")),
    case("receiver_rebound_fn_compound", "g := [[1]]\nfn(f()) { g = [[9]]; return 5 }\nkeep := g\n"
         "g[0][0] += f()\nprint(g.stringify())\nprint(keep.stringify())", "[[9]]\n[[6]]\n-- exit 0\n",
         (D2, "[[6]]\n[[1]]\n-- exit 0\n")),
    # Review of C105-C108, P2-1: the rhs replaces an intermediate container, for every callee (the
    # receiver-rebinding lambda case above is legacy-equal; these are not): the update goes to the
    # container read first.
    case("order_compound_intermediate_replaced_fn", "g := [[1, 2], [3, 4]]\nrow := g[0]\n"
         "fn(f()) { g[0] = [100, 200]; return 5 }\ng[0][1] += f()\nprint(g.stringify())\nprint(row.stringify())",
         "[[100,200],[3,4]]\n[1,7]\n-- exit 0\n", (D2, "[[100,7],[3,4]]\n[100,7]\n-- exit 0\n")),
    case("order_compound_intermediate_replaced_lambda", "g := [[1, 2], [3, 4]]\nrow := g[0]\n"
         "f := () => { g[0] = [100, 200]; return 5 }\ng[0][1] += f()\nprint(g.stringify())\nprint(row.stringify())",
         "[[100,200],[3,4]]\n[1,7]\n-- exit 0\n", (D2, "[[100,7],[3,4]]\n[100,7]\n-- exit 0\n")),
    case("order_compound_object_intermediate_replaced", 'o := {"m": {"x": [1, 60]}}\n'
         'fn(f()) { o["m"] = {"x": [50, 60]}; return 5 }\no["m"]["x"][0] += f()\nprint(o.stringify())',
         '{"m":{"x":[50,60]}}\n-- exit 0\n', (D2, '{"m":{"x":[6,60]}}\n-- exit 0\n')),
    case("order_compound_object_intermediate_replaced_lambda", 'o := {"m": {"x": [1, 60]}}\n'
         'f := () => { o["m"] = {"x": [50, 60]}; return 5 }\no["m"]["x"][0] += f()\nprint(o.stringify())',
         '{"m":{"x":[50,60]}}\n-- exit 0\n', (D2, '{"m":{"x":[6,60]}}\n-- exit 0\n')),
    # an inline assignment in the rhs rebinds the key: the key read first is used
    case("order_compound_inline_key", "a := [1, 2]\ni := 0\na[i] += (i = 1)\nprint(a.stringify())",
         "[2,2]\n-- exit 0\n", (D2, "[1,2]\n-- exit 0\n")),
    case("order_compound_inline_key_nested", "g := [[1, 2], [3, 4]]\ni := 0\ng[i][i] += (i = 1) + 10\n"
         "print(g.stringify())", "[[12,2],[3,4]]\n-- exit 0\n", (D2, "[[1,2],[3,12]]\n-- exit 0\n")),
    case("compound_rhs_shrinks", "a := [1, 2]\nf := () => { a.pop(); return 5 }\na[1] += f()\nprint(a.stringify())",
         "-- exit 1\nerror: {name}:3:1: index out of range: 1\n"),
    # ---- aliases (B5) ----
    case("alias_two_names_object", 'o := {"k": 1}\np := o\np["k"] = 2\np["n"] += 0\nprint(o.stringify())',
         "-- exit 1\nerror: {name}:4:1: JSON object has no key 'n'\n"),
    case("alias_two_names_object_visible", 'o := {"k": 1}\np := o\np["k"] = 2\np["k"] += 1\nprint(o.stringify())',
         '{"k":3}\n-- exit 0\n', (B5, '{"k":1}\n-- exit 0\n')),
    case("alias_nested_extracted", "g := [[1]]\nh := g[0]\ng[0][0] = 5\nh[0] += 1\nprint(h.stringify())\n"
         "print(g.stringify())", "[6]\n[[6]]\n-- exit 0\n"),
    case("alias_nested_object", 'o := {"k": [1]}\nc := o\no["k"][0] = 3\nprint(c.stringify())',
         '{"k":[3]}\n-- exit 0\n', (B5, '{"k":[1]}\n-- exit 0\n')),
    case("alias_across_call", 'fn(f(o)) { o["k"] = 2; return 0 }\nfn(h(g)) { g[0][0] += 2; return 0 }\n'
         'x := {"k": 1}\ny := [[1]]\nf(x)\nh(y)\nprint(x.stringify())\nprint(y.stringify())',
         '{"k":2}\n[[3]]\n-- exit 0\n', (B5ARG, '{"k":1}\n[[1]]\n-- exit 0\n')),
    case("alias_loop_variable", 'arr := [{"k": 1}, {"k": 2}]\nfor (e : arr) { e["k"] = 9 }\nprint(arr.stringify())',
         '[{"k":9},{"k":9}]\n-- exit 0\n', (B5LOOP, '[{"k":1},{"k":2}]\n-- exit 0\n')),
    case("alias_loop_variable_compound", "arr := [[1, 2], [3, 4]]\nfor (e : arr) { e[0] += 10 }\n"
         "print(arr.stringify())", "[[11,2],[13,4]]\n-- exit 0\n"),
    case("alias_function_global", "g := [[1]]\nfn(f()) { g[0][0] = 7; g[0][0] += 1; return 0 }\nf()\n"
         "print(g.stringify())", "[[8]]\n-- exit 0\n"),
    case("alias_closure", "g := [[1]]\nh := () => { g[0][0] = 7; g[0][0] *= 3 }\nh()\nprint(g.stringify())",
         "[[21]]\n-- exit 0\n"),
    case("alias_function_local_closure", 'fn(mk()) { m := {"a": [0]}; s := () => { m["a"][0] += 1 }; s(); s();'
         ' return m }\nprint(mk().stringify())', '{"a":[2]}\n-- exit 0\n'),
    # ---- binding-only const (Q2 B) ----
    case("const_element_writes", "const g := [[1]]\ng[0][0] = 2\ng[0][0] += 2\nprint(g.stringify())",
         "[[4]]\n-- exit 0\n", (Q2B, "-- exit 1\nerror: {name}:2:1: cannot assign to const binding: g\n")),
    # ---- failures: legacy's message and position ----
    case("fail_intermediate_range", "g := [[1]]\ng[5][0] = 7", "-- exit 1\nerror: {name}:2:1: index out of range: 5\n"),
    case("fail_final_range", "g := [[1]]\ng[0][5] = 7", "-- exit 1\nerror: {name}:2:1: index out of range: 5\n"),
    case("fail_range_variable", "g := [[1]]\ni := 3\ng[i][0] = 1",
         "-- exit 1\nerror: {name}:3:1: index out of range: i\n"),
    case("fail_missing_intermediate", 'o := {"a": {}}\no["q"]["z"] = 1',
         "-- exit 1\nerror: {name}:2:1: object has no member: q\n"),
    case("fail_missing_intermediate_variable", 'o := {"a": {}}\nk := "q"\no[k]["z"] = 1',
         "-- exit 1\nerror: {name}:3:1: object has no member: q\n"),
    case("fail_fraction_index", "g := [[1]]\ni := 0.5\ng[0][i] = 7",
         "-- exit 1\nerror: {name}:3:1: assignment index does not address an array or object: i\n"),
    case("fail_negative_index", "g := [[1]]\ni := -1\ng[i][0] = 7",
         "-- exit 1\nerror: {name}:3:1: assignment index does not address an array or object: i\n"),
    case("fail_compound_range", "g := [[1]]\ng[3][0] += 1",
         "-- exit 1\nerror: {name}:2:1: JSON array index 3 is out of range in 'g[3][0]'\n"),
    case("fail_compound_final_range", "g := [[1]]\ng[0][3] += 1",
         "-- exit 1\nerror: {name}:2:1: JSON array index 3 is out of range in 'g[0][3]'\n"),
    case("fail_compound_missing", 'o := {"a": {}}\no["a"]["z"] += 1',
         "-- exit 1\nerror: {name}:2:1: JSON object has no key 'z'\n"),
    case("fail_compound_division", "a := [7]\na[0] /= 0", "-- exit 1\nerror: {name}:2:1: division by zero\n"),
    case("fail_compound_modulo_zero", "a := [7]\na[0] %= 0", "-- exit 1\nerror: {name}:2:1: modulo by zero\n"),
    case("fail_compound_modulo_fraction", "g := [[1.5]]\ng[0][0] %= 1",
         "-- exit 1\nerror: {name}:2:1: modulo requires integer-valued operands\n"),
    case("fail_compound_overflow", "a := [9223372036854775807]\na[0] += 1",
         "-- exit 1\nerror: {name}:2:1: signed 64-bit integer overflow\n"),
    case("fail_loop_wording", "n := 0\ng := [[1]]\nwhile (n < 1) {\ng[0][5] = 7\nn += 1\n}",
         "-- exit 1\nerror: {name}:4:1: index out of range: 5\n",
         (D018C, "-- exit 1\nerror: {name}:4:1: invalid index target\n")),
    case("fail_loop_wording_missing", 'n := 0\no := {"a": {}}\nwhile (n < 1) {\no["q"]["z"] = 1\nn += 1\n}',
         "-- exit 1\nerror: {name}:4:1: object has no member: q\n",
         (D018C, "-- exit 1\nerror: {name}:4:1: invalid index target\n")),
    # a session continues after an error: the target is unchanged by every failed assignment
    case("atomic_session_failures", 'g := [[1, 2], [3]]\no := {"a": {"b": 1}}\nf := [[1.5]]\nn := [7]\n'
         'g[5][0] = 1\ng[0][7] = 1\ni := 0.5\ng[0][i] = 1\no["x"]["y"] = 1\ng[0][9] += 1\no["a"]["z"] += 1\n'
         'f[0][0] %= 1\nn[0] /= 0\nprint(g.stringify())\nprint(o.stringify())\nprint(f.stringify())\n'
         'print(n.stringify())',
         '[[1,2],[3]]\n{"a":{"b":1}}\n[[1.5]]\n[7]\n-- exit 0\nerror: index out of range: 5\n'
         'error: index out of range: 7\nerror: assignment index does not address an array or object: i\n'
         "error: object has no member: x\nerror: JSON array index 9 is out of range in 'g[0][9]'\n"
         "error: JSON object has no key 'z'\nerror: modulo requires integer-valued operands\n"
         "error: division by zero\n", session=True),
    case("atomic_session_overflow", "a := [9223372036854775807, 1]\na[0] += 1\na[1] += 1\nprint(a.stringify())",
         "[9223372036854775807,2]\n-- exit 0\nerror: signed 64-bit integer overflow\n", session=True),
    case("session_forms", 'g := [[1, 2], [3]]\ng[0][1] = 9\no := {"a": {"b": 1}}\no["a"]["b"] = 5\n'
         'o["a"]["c"] = 6\ng[0][0] += 10\no["a"]["b"] *= 2\nprint(g.stringify())\nprint(o.stringify())',
         '[[11,9],[3]]\n{"a":{"b":10,"c":6}}\n-- exit 0\n', session=True),
    # ---- cycles: refused through each store form, catchable, nothing written ----
    case("cycle_nested", "g := [[1]]\ntry { g[0][0] = g } catch (e) { print(e.code) }\nprint(g.stringify())",
         "runtime.reference_cycle\n[[1]]\n-- exit 0\n", (CYCLE, "[[[[1]]]]\n-- exit 0\n")),
    case("cycle_object", 'o := {"k": 1}\ntry { o["k"] = o } catch (e) { print(e.code) }\nprint(o.stringify())',
         'runtime.reference_cycle\n{"k":1}\n-- exit 0\n', (CYCLE, '{"k":{"k":1}}\n-- exit 0\n')),
    case("cycle_object_new_key", 'o := {"k": 1}\ntry { o["n"] = [o] } catch (e) { print(e.code) }\n'
         'print(o.stringify())', 'runtime.reference_cycle\n{"k":1}\n-- exit 0\n',
         (CYCLE, '{"k":1,"n":[{"k":1}]}\n-- exit 0\n')),
    case("cycle_nested_object", 'o := {"a": {"b": 1}}\ntry { o["a"]["b"] = o } catch (e) { print(e.code) }\n'
         'print(o.stringify())', 'runtime.reference_cycle\n{"a":{"b":1}}\n-- exit 0\n',
         (CYCLE, '{"a":{"b":{"a":{"b":1}}}}\n-- exit 0\n')),
    case("cycle_inner_alias", "g := [[1], [2]]\nh := g[1]\ntry { g[0][0] = g } catch (e) { print(e.code) }\n"
         "try { h[0] = g } catch (e) { print(e.code) }\nprint(g.stringify())",
         "runtime.reference_cycle\nruntime.reference_cycle\n[[1],[2]]\n-- exit 0\n",
         (CYCLE, "[[[[1],[2]]],[[[[[1],[2]]],[2]]]]\n-- exit 0\n")),
    case("cycle_uncaught", "g := [[1]]\ng[0][0] = g",
         "-- exit 1\nerror: {name}:2:1: reference cycle: the stored value would contain itself\n",
         (CYCLE, "-- exit 0\n")),
    # ---- heap budget: exit 70, no later statement runs ----
    # The only allocations after "before" are the object stores (a new atom and member each).
    case("heap_budget_object_store", 'keys := []\nn := 0\nwhile (n < 4000) { keys.push("key-number-" + n); n += 1 }\n'
         'o := {"k": 0}\nprint("before")\nn = 0\nwhile (n < 4000) { k := keys[n]; o[k] = n; n += 1 }\nprint("after")',
         "before\n-- exit 70\nerror: {name}: NIFT_VM internal failure: memory budget exceeded (the script was not "
         "run on the legacy engine)\n", None, env={"NIFT_VM_HEAP_BUDGET": "500000"}),
    case("heap_budget_control", 'keys := []\nn := 0\nwhile (n < 4000) { keys.push("key-number-" + n); n += 1 }\n'
         'o := {"k": 0}\nprint("before")\nn = 0\nwhile (n < 4000) { k := keys[n]; o[k] = n; n += 1 }\n'
         'print(o.size())', "before\n4001\n-- exit 0\n"),
    # ---- still refused (out of scope), before any effect ----
    case("refused_member_dot", 'o := {"k": 1}\nprint("x")\no.k = 5',
         "-- exit 1\nerror: {name}:3:1: not compiled for the VM (static-error:member-assign): a statically certain"
         " program error the VM does not reproduce yet (member-assign) is not supported by the Runtime 2.0 VM yet\n",
         ("legacy error", "x\n-- exit 1\nerror: {name}:3:1: member assignment requires a struct instance: o\n")),
    case("refused_computed_key", "a := [[1, 2]]\ni := 0\nprint(\"x\")\na[i + 1][0] = 9",
         refused(4, 1, "index-target", "this index assignment target"), ("refusal", "x\n-- exit 1\n"
         "error: {name}:4:1: index out of range: i + 1\n")),
    case("refused_dotted_key", 'o := {}\nprint("x")\no["a.b"] = 1',
         refused(3, 1, "index-target", "this index assignment target"),
         ("refusal", 'x\n-- exit 1\nerror: {name}:3:1: member assignment requires a struct instance: o["a\n')),
    case("refused_increment", "a := [1]\nprint(\"x\")\na[0]++\nprint(a.stringify())",
         refused(3, 1, "incdec-target", "this increment or decrement target"), ("refusal", "x\n[2]\n-- exit 0\n")),
    case("refused_depth4", "g := [[[[1]]]]\nprint(\"x\")\ng[0][0][0][0] = 2\nprint(g.stringify())",
         refused(3, 1, "index-assign-target", "this index assignment target"), ("refusal", "x\n[[[[2]]]]\n-- exit 0\n")),
    case("refused_mixed_kinds", "g := [[1], 5]\ni := 1\nprint(\"x\")\ng[i][0] = 7",
         refused(4, 1, "type:element-assignment", "an operation whose operand types the VM cannot prove "
                 "(element-assignment)"), ("refusal", "x\n-- exit 1\n"
                 "error: {name}:4:1: assignment index does not address an array or object: 0\n")),
    # Review P2-2: an object with a number key, and a non-`+=` operator on two strings, stay refused.
    case("refused_object_number_key", 'o := {"a": 1}\nprint("x")\no[0] = 5',
         refused(3, 1, "type:element-assignment", "an operation whose operand types the VM cannot prove "
                 "(element-assignment)"), ("refusal", "x\n-- exit 1\n"
                 "error: {name}:3:1: assignment index does not address an array or object: 0\n")),
    case("refused_string_compound_sub", 'a := ["x"]\nprint("x")\na[0] -= "y"',
         refused(3, 1, "type:compound-assignment", "an operation whose operand types the VM cannot prove "
                 "(compound-assignment)"), ("refusal", "x\n-- exit 1\n"
                 "error: {name}:3:1: arithmetic operators require numeric operands\n")),
    case("refused_string_compound_mul", 'a := ["x"]\nprint("x")\na[0] *= "y"',
         refused(3, 1, "type:compound-assignment", "an operation whose operand types the VM cannot prove "
                 "(compound-assignment)"), ("refusal", "x\n-- exit 1\n"
                 "error: {name}:3:1: arithmetic operators require numeric operands\n")),
    case("refused_string_compound_div", 'a := ["x"]\nprint("x")\na[0] /= "y"',
         refused(3, 1, "type:compound-assignment", "an operation whose operand types the VM cannot prove "
                 "(compound-assignment)"), ("refusal", "x\n-- exit 1\n"
                 "error: {name}:3:1: arithmetic operators require numeric operands\n")),
    case("refused_string_compound_mod", 'a := ["x"]\nprint("x")\na[0] %= "y"',
         refused(3, 1, "type:compound-assignment", "an operation whose operand types the VM cannot prove "
                 "(compound-assignment)"), ("refusal", "x\n-- exit 1\n"
                 "error: {name}:3:1: arithmetic operators require numeric operands\n")),
    case("refused_compound_mixed", 'a := [1, "s"]\nprint("x")\na[1] += 1',
         refused(3, 1, "type:compound-assignment", "an operation whose operand types the VM cannot prove "
                 "(compound-assignment)"), ("refusal", "x\n-- exit 1\n"
                 "error: {name}:3:1: arithmetic operators require numeric operands\n")),
    case("future_object_member", 'fn[async](f(x)) { return x + 1 }\nhs := {"a": 0}\nhs["a"] = f(1)\n'
         'print(await hs["a"])', "2\n-- exit 0\n"),
    # CP22's future-flow admission is unchanged: a future stored through a path stays refused.
    case("refused_future_nested", 'fn[async](f(x)) { return x + 1 }\nhs := [[0]]\nprint("x")\nhs[0][0] = f(1)\n'
         'print(await hs[0][0])', refused(4, 12, "coro-future-flow:Assign", "this coroutine or async usage"),
         ("refusal", "x\n2\n-- exit 0\n")),
    case("refused_compound_expression", "a := [1]\nprint(\"x\")\nprint(a[0] += 5)",
         refused(3, 7, "expression:assignment-target", "a language feature"),
         ("refusal", "x\n6\n-- exit 0\n")),
]


def run(nift, c, legacy):
    work = tempfile.mkdtemp(prefix="assign-family-")
    try:
        name = c["name"] + (".in" if c["session"] else ".f")
        path = os.path.join(work, name)
        with open(path, "w") as fh:
            fh.write(c["src"] + "\n")
        stats = os.path.join(work, "s.jsonl")
        env = {"PATH": "/usr/bin:/bin", "HOME": work, "TMPDIR": work, "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
               "NO_COLOR": "1", "NIFT_VM": "0" if legacy else "require", "NIFT_VM_STATS": stats, **c["env"]}
        if legacy:
            env.pop("NIFT_VM_HEAP_BUDGET", None)
        if c["session"]:
            cmd, stdin = ["timeout", "60", nift, "-i"], open(path, "rb").read()
        else:
            cmd, stdin = ["timeout", "60", nift, name], b""
        p = subprocess.run(cmd, cwd=work, env=env, input=stdin, capture_output=True)
        defects = 0
        if os.path.exists(stats):
            for line in open(stats):
                try:
                    defects += int(json.loads(line).get("dispatch_defects", 0))
                except ValueError:
                    pass
        out = p.stdout.decode("utf-8", "replace")
        err = p.stderr.decode("utf-8", "replace").replace(work + "/", "")
        errors = [l for l in err.splitlines() if l.startswith("error:")]
        text = out + ("" if out.endswith("\n") or not out else "\n") + "-- exit %d\n" % p.returncode
        return text + "".join(e + "\n" for e in errors), out + err, p.returncode, defects
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--dump", action="store_true")
    args = ap.parse_args()
    failures = []
    names = set()
    for c in CASES:
        if c["name"] in names:
            failures.append("%s: duplicate case name" % c["name"])
        names.add(c["name"])
        fname = c["name"] + (".in" if c["session"] else ".f")
        vm, vm_raw, vm_rc, defects = run(args.nift, c, False)
        leg, _, _, _ = run(args.nift, c, True)
        if args.dump:
            print("== %s\n-- vm\n%s-- legacy\n%s" % (c["name"], vm, leg))
            continue
        want_vm = c["vm"].replace("{name}", fname)
        pinned_70 = "-- exit 70\n" in want_vm
        internal = INTERNAL.search(vm_raw.replace("NIFT_VM internal failure: memory budget exceeded", "")
                                   if pinned_70 else vm_raw)
        if (vm_rc == 70 and not pinned_70) or vm_rc < 0 or vm_rc >= 128 or internal or defects:
            failures.append("%s: internal failure (exit %d, defects %d)" % (c["name"], vm_rc, defects))
        if vm != want_vm:
            failures.append("%s: VM output differs\n--- want\n%s--- got\n%s" % (c["name"], want_vm, vm))
        if c["legacy"] is None:
            continue  # legacy has no heap budget: the budget case is VM-only
        if c["legacy"] == SAME:
            if leg != want_vm:
                failures.append("%s: legacy differs from the VM pin (no approved difference)\n--- vm pin\n%s"
                                "--- legacy\n%s" % (c["name"], want_vm, leg))
        else:
            decision, pin = c["legacy"]
            want_leg = pin.replace("{name}", fname)
            if leg != want_leg:
                failures.append("%s: legacy output differs from its pin (%s)\n--- want\n%s--- got\n%s"
                                % (c["name"], decision, want_leg, leg))
            if want_leg == want_vm:
                failures.append("%s: listed as a difference (%s) but the pins are equal" % (c["name"], decision))
    if args.dump:
        return 0
    for f in failures:
        print("FAIL " + f)
    if failures:
        print("FAIL assignment family: %d failure(s) over %d cases" % (len(failures), len(CASES)))
        return 1
    diffs = sum(1 for c in CASES if c["legacy"] not in (SAME, None))
    print("PASS assignment family: %d cases (%d legacy-equal, %d pinned differences or refusals)"
          % (len(CASES), len(CASES) - diffs, diffs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
