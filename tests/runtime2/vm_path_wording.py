#!/usr/bin/env python3
"""Error wording on member/index paths: regression fixtures for the open VM
defects recorded in docs/runtime-2/certification.md ("Open VM defects: error
wording on paths", found during records20k stage 4). Each program runs
under NIFT_VM=require (it must run on the VM) and is compared with legacy
byte for byte; the ones marked d018c are compared under the approved
D-018c difference (legacy reports the statement, with `return: ` before a
return's message; the message must be identical).

  D1  an index out of range inside a longer path: legacy quotes the whole
      member/index chain (`a[5].y` -> 'a[5].y'); a method call ends it.
  D3  a missing member whose receiver has an index other than plain digits
      (`a[j].k`, `w["y"].k`, `a[ 1 ].k`): legacy's textual evaluator says
      `JSON value 'a[j]' has no member 'k'`; with digit indexes only, or on
      its prepared evaluator (loop code, functions called from it, except a
      path that is a print argument by itself), `value has no member: k`.

  vm_path_wording.py --nift ./nift
"""
import argparse
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vm_records_coverage import d018c_equal, run  # noqa: E402

D1 = [
    ("d1_plain", 'a := [{"y": {"z": 1}}]\nv := a[5]\nprint(v)\n', None),
    ("d1_plus", 'a := [{"y": {"z": 1}}]\nv := a[5].y + 1\nprint(v)\n', None),
    ("d1_deep", 'a := [{"y": {"z": 1}}]\nv := a[5].y.z\nprint(v)\n', None),
    ("d1_print", 'a := [{"y": {"z": 1}}]\nprint(a[5].y)\n', None),
    ("d1_nested", 'a := [[{"y": 1}]]\nv := a[0][5].y\nprint(v)\n', None),
    ("d1_index_last", 'a := [{"y": [1]}]\nv := a[0].y[3]\nprint(v)\n', None),
    ("d1_method", 'a := [{"y": {"z": 1}}]\nv := a[5].stringify()\nprint(v)\n', None),
    ("d1_chain_method", 'a := [{"k": 1}, {"j": 2}]\nprint(a[5].k.stringify())\n', None),
    ("d1_loop_print", 'a := [{"k": 1}, {"j": 2}]\nj := 1\ni := 0\nwhile (i < 1) {\n  print(a[j * 5].k)\n  i += 1\n}\n',
     None),
    ("d1_condition", 'a := [{"y": {"z": 1}}]\nif (a[5].y.z == 1) {\n  print(1)\n}\n', "d018c"),
]

# D3: a missing member after an index whose key is not plain digits.
D3 = [
    ('d3_print_index_name', 'a := [{"k": 1}, {"j": 2}]\nj := 1\no := {"k": 1, "n": {"m": 1}}\nprint(a[j].k)\n', None),
    ('d3_print_digit_index', 'a := [{"k": 1}, {"j": 2}]\nj := 1\no := {"k": 1, "n": {"m": 1}}\nprint(a[1].k)\n',
     None),
    ('d3_print_member_only', 'a := [{"k": 1}, {"j": 2}]\nj := 1\no := {"k": 1, "n": {"m": 1}}\nprint(o.zz)\n', None),
    ('d3_decl', 'a := [{"k": 1}, {"j": 2}]\nj := 1\no := {"k": 1, "n": {"m": 1}}\nv := a[j].k\n', None),
    ('d3_concat', 'a := [{"k": 1}, {"j": 2}]\nj := 1\no := {"k": 1, "n": {"m": 1}}\ns := "x" + a[j].k\n', None),
    ('d3_array_literal', 'a := [{"k": 1}, {"j": 2}]\nj := 1\no := {"k": 1, "n": {"m": 1}}\nv := [a[j].k]\n', None),
    ('d3_stringify_receiver', 'a := [{"k": 1}, {"j": 2}]\nj := 1\no := {"k": 1, "n": {"m": 1}}\n'
     'print(a[j].k.stringify())\n', None),
    ('d3_inner_member', 'a := [{"k": {"m": 1}}, {"k": {"z": 2}}]\nj := 1\nprint(a[j].k.m)\n', None),
    ('d3_two_indexes', 'a := [{"k": 1}, {"j": 2}]\nj := 1\nb := [[{"k": 1}], [{"j": 2}]]\n'
     'w := {"x": {"k": 1}, "y": {"j": 2}}\nq := "y"\nr := [{"k": {"m": 1}}, {"k": {"z": 2}}]\nv := 0\nz := []\n'
     'print(b[j][0].k)\n', None),
    ('d3_string_key', 'a := [{"k": 1}, {"j": 2}]\nj := 1\nb := [[{"k": 1}], [{"j": 2}]]\n'
     'w := {"x": {"k": 1}, "y": {"j": 2}}\nq := "y"\nr := [{"k": {"m": 1}}, {"k": {"z": 2}}]\nv := 0\nz := []\n'
     'u := w["y"].k\n', None),
    ('d3_name_key', 'a := [{"k": 1}, {"j": 2}]\nj := 1\nb := [[{"k": 1}], [{"j": 2}]]\n'
     'w := {"x": {"k": 1}, "y": {"j": 2}}\nq := "y"\nr := [{"k": {"m": 1}}, {"k": {"z": 2}}]\nv := 0\nz := []\n'
     'u := w[q].k == 1\n', None),
    ('d3_spaced_digit', 'a := [{"k": 1}, {"j": 2}]\nj := 1\nprint(a[ 1 ].k)\n', None),
    ('d3_expression_key', 'a := [{"k": 1}, {"j": 2}]\nj := 1\nprint(a[0 + 1].k)\n', None),
    ('d3_before_name_index', 'a := [{"k": 1}, {"j": 2}]\nj := 1\nprint(a[1].zz[j])\n', None),
    ('d3_fn_decl', 'fn(g(b, x)) {\n  u := b[x].k\n  return u\n}\nh := [{"k": 1}, {"j": 2}]\nprint(g(h, 1))\n', None),
    ('d3_fn_print', 'fn(g(b, x)) {\n  print(b[x].k)\n  return 0\n}\nh := [{"k": 1}, {"j": 2}]\ng(h, 1)\n', None),
    ('d3_loop_print', 'a := [{"k": 1}, {"j": 2}]\nj := 1\no := {"k": 1, "n": {"m": 1}}\ni := 0\n'
     'while (i < 1) {\n  print(a[j].k)\n  i += 1\n}\n', None),
    ('d3_loop_print_paren', 'a := [{"k": 1}, {"j": 2}]\nj := 1\ni := 0\nwhile (i < 1) {\n  print((a[j].k))\n'
     '  i += 1\n}\n', None),
    ('d3_loop_decl', 'a := [{"k": 1}, {"j": 2}]\nj := 1\nb := [[{"k": 1}], [{"j": 2}]]\n'
     'w := {"x": {"k": 1}, "y": {"j": 2}}\nq := "y"\nr := [{"k": {"m": 1}}, {"k": {"z": 2}}]\nv := 0\nz := []\n'
     'i := 0\nwhile (i < 1) {\n  u := a[j].k\n  i += 1\n}\n', None),
    ('d3_loop_print_concat', 'a := [{"k": 1}, {"j": 2}]\nj := 1\ni := 0\nwhile (i < 1) {\n'
     '  print("x" + a[j].k)\n  i += 1\n}\n', None),
    ('d3_loop_called_print', 'fn(g(b, x)) {\n  print(b[x].k)\n  return 0\n}\nfn(h(p)) {\n  return p\n}\n'
     'a := [{"k": 1}, {"j": 2}]\nj := 1\ni := 0\nwhile (i < 1) {\n  g(a, 1)\n  i += 1\n}\n', None),
    ('d3_loop_called_decl', 'fn(g(b, x)) {\n  u := b[x].k\n  return u\n}\nh := [{"k": 1}, {"j": 2}]\ni := 0\n'
     'while (i < 1) {\n  print(g(h, 1))\n  i += 1\n}\n', None),
    ('d3_if', 'a := [{"k": 1}, {"j": 2}]\nj := 1\no := {"k": 1, "n": {"m": 1}}\nif (a[j].k == 1) {\n'
     '  print(1)\n}\n', 'd018c'),
    ('d3_fn_return', 'fn(g(b, x)) {\n  return b[x].k\n}\nh := [{"k": 1}, {"j": 2}]\nprint(g(h, 1))\n', 'd018c'),
]

CASES = D1 + D3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--timeout", type=float, default=60)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    failures = []
    work = tempfile.mkdtemp(prefix="nift-vm-path-wording-")
    try:
        for name, source, policy in CASES:
            d = tempfile.mkdtemp(dir=work)
            with open(os.path.join(d, "p.f"), "w") as f:
                f.write(source)
            legacy, _ = run(nift, d, None, a.timeout)
            vm, st = run(nift, d, "require", a.timeout)
            if not st or st["outcome"] not in ("vm", "vm-error"):
                failures.append(f"{name}: not on the VM ({st and (st['outcome'], st['reason'])})")
            elif legacy[2] == 0:
                failures.append(f"{name}: legacy no longer fails ({legacy})")
            elif policy == "d018c" and not d018c_equal(legacy, vm):
                failures.append(f"{name}: differs beyond D-018c\n  legacy {legacy}\n  vm     {vm}")
            elif policy is None and vm != legacy:
                failures.append(f"{name}: output differs\n  legacy {legacy}\n  vm     {vm}")
            shutil.rmtree(d, ignore_errors=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures:
        print("FAIL " + f)
    print(("PASS" if not failures else "FAIL") + f" vm path wording: {len(CASES)} programs on the VM, identical to legacy "
          f"({sum(1 for c in CASES if c[2])} under D-018c); {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
