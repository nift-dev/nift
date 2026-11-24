#!/usr/bin/env python3
"""Selector value/capture/location and error contracts, with optional A/B oracle."""
import os
from pathlib import Path
import subprocess
import tempfile

binary = str(Path(os.environ.get('NIFT', './nift')).resolve())
baseline = os.environ.get('NIFT_BASELINE')
cases = [
 ('numeric map', 'print([3,1,2].map(x => x).stringify())', '[3,1,2]\n'),
 ('numeric sort', 'print([3,1,2].sort_by(x => x).stringify())', '[1,2,3]\n'),
 ('string sort', 'print(["b","a"].sort_by(x => x).stringify())', '["a","b"]\n'),
 ('mixed identity', 'print([null,true,"x",[1],{"a":2}].map(x => x).stringify())', '[null,true,"x",[1],{"a":2}]\n'),
 ('exact scalar', 'f := x => x\nprint(f(9007199254740993))', '9007199254740993\n'),
 ('parameter shadows capture', 'x := 99\nf := x => x\nprint(f(3)); print(x)', '3\n99\n'),
 ('mutable scalar capture', 'v := 2\nf := x => v\nv = 7\nprint([1,2].map(f).stringify())', '[7,7]\n'),
 ('returned callable', 'fn(mk()) { return x => x }\nf := mk(); print(f(4))', '4\n'),
 ('nested identity', 'f := x => x\ng := y => f(y)\nprint([1,2].map(g).stringify())', '[1,2]\n'),
 ('identity reference', 'a := [2]\nf := x => x\ny := f(a[0]); a[0] = 9; print(y)', '2\n'),
 ('captured location realloc', 'a := [3]\nfor(x : a) { f := y => x; a.push(4); print(f(0)) }', '3\n'),
 ('named precedence', 'fn(x()) { return 8 }\nf := x => x\nprint(type(f(3)))', 'int\n'),
 ('argument side effect', 'n := 1\nf := x => x\nprint(f(n++)); print(n)', '1\n2\n'),
 ('escaped captures', 'fn(mk(v)) { return x => v }\na := mk(2); b := mk(8); print(a(0)); print(b(0))', '2\n8\n'),
 ('scalar index', 'a := [4,2,9]\nprint([2,0,1].map(x => a[x]).stringify())', '[9,4,2]\n'),
 ('parameter index', 'print([[3],[1]].sort_by(x => x[0]).stringify())', '[[1],[3]]\n'),
 ('dynamic index', 'i := 0\nf := x => x[i]\nprint(f([2,7])); i = 1; print(f([2,7]))', '2\n7\n'),
 ('indexed aggregate result', 'f := x => x[0]\nprint(f([[2,3]]).stringify())', '[2,3]\n'),
 ('indexed captured mutation', 'a := [2,7]\nf := x => a[x]\nprint(f(0)); a[0] = 9; a.push(4); print(f(0)); print(f(2))', '2\n9\n4\n'),
 ('indexed argument order', 'i := 0\nf := x => x[i]\nprint(f([8,9])); i += 1; print(f([8,9]))', '8\n9\n'),
 ('indexed object fallback', 'f := x => x["a"]\nprint(f({"a":3}))', '3\n'),
 ('indexed returned location', 'a := [[1]]\nf := x => x[0]\nb := f(a); a.push([9]); b.push(2); print(a.stringify())', '[[1],[9]]\n'),
 ('indexed root shadowing', 'a := [9]\nf := a => a[0]\nprint(f([3])); print(a[0])', '3\n9\n'),
 ('indexed type transition', 'a := [3]\nf := x => a[x]\nprint(f(0)); a = [7]; print(f(0))', '3\n7\n'),
 ('mapped aggregate independence', 'a := [[1],[2]]\nb := a.map(x => x); b[0][0] = 3; print(a.stringify()); print(b.stringify())', '[[1],[2]]\n[[3],[2]]\n'),
 ('mapped returned callbacks', 'fn(mk(v)) { return x => v }\na := [2,7].map(x => mk(x)); f := a[0]; g := a[1]; print(f(0)); print(g(0))', '2\n7\n'),
 ('filter aggregate independence', 'a := [[1],[2]]\nb := a.filter(x => true); b[0][0] = 3; print(a.stringify()); print(b.stringify())', '[[1],[2]]\n[[3],[2]]\n'),
 ('group order', 'print([3,1,3].group_by(x => x).stringify())', '{"3":[3,3],"1":[1]}\n'),
 ('group rendered collision', 'print([1,"1",true,"true"].group_by(x => x).stringify())', '{"1":[1,"1"],"true":[true,"true"]}\n'),
 ('group aggregate independence', 'a := [{"k":1},{"k":2}]\nb := a.group_by(x => x.k); b["1"][0]["k"] = 3; print(a.stringify()); print(b.stringify())', '[{"k":1},{"k":2}]\n{"1":[{"k":3}],"2":[{"k":2}]}\n'),
 ('group callback effects', 'n := 0\nfn(k(x)) { n += 1; return n % 2 }\nprint([5,6,7].group_by(k).stringify()); print(n)', '{"1":[5,7],"0":[6]}\n3\n'),
 ('numeric member', 'print([{"v":3},{"v":1}].map(x => x.v).stringify())', '[3,1]\n'),
 ('exact member', 'f := x => x.v\nprint(f({"v":9007199254740993}))', '9007199254740993\n'),
 ('member arithmetic', 'print([{"v":3},{"v":1}].map(x => x.v % 2).stringify())', '[1,1]\n'),
 ('member capture rebinding', 'a := {"v":2}\nf := x => a.v\nprint(f(0)); a = {"v":7}; print(f(0))', '2\n7\n'),
 ('member parameter shadow', 'a := {"v":9}\nf := a => a.v\nprint(f({"v":3})); print(a.v)', '3\n9\n'),
 ('member type fallback', 'f := x => x.v\nprint(f({"v":"s"})); print(f({"v":true})); print(f({"v":3}))', 's\ntrue\n3\n'),
 ('member aggregate independence', 'a := {"v":[1]}\nf := x => x.v\nb := f(a); b.push(2); print(a.stringify()); print(b.stringify())', '{"v":[1]}\n[1,2]\n'),
 ('sort aggregate independence', 'a := [{"v":3},{"v":1}]\nb := a.sort_by(x => x.v); b[0]["v"] = 9; print(a.stringify()); print(b.stringify())', '[{"v":3},{"v":1}]\n[{"v":9},{"v":3}]\n'),
 ('sort stable ties', 'print([{"v":1,"id":2},{"v":1,"id":3},{"v":0,"id":4}].sort_by(x => x.v).stringify())', '[{"v":0,"id":4},{"v":1,"id":2},{"v":1,"id":3}]\n'),
 ('member captured growth', 'a := {"v":2}\nf := x => a.v\nprint(f(0)); a["v"] = 7; a["more"] = 3; print(f(0))', '2\n7\n'),
 ('member exact comparison', 'f := x => x.v == 9007199254740993\nprint(f({"v":9007199254740993})); print(f({"v":9007199254740992}))', 'true\nfalse\n'),
 ('member zero division', 'f := x => x.v / 0\nprint(f({"v":3}))', None),
 ('member zero modulo', 'f := x => x.v % 0\nprint(f({"v":3}))', None),
 ('member missing operand', 'f := x => x.v + missing\nprint(f({"v":3}))', None),
 ('member missing arithmetic', 'f := x => x.missing + 1\nprint(f({"v":3}))', None),
 ('missing member', 'f := x => x.missing\nprint(f({"v":3}))', None),
 ('null member', 'f := x => x.v\nprint(f(null))', None),
 ('bare nested-array argument alias', 'fn(add(x)) { x.push(9) }\na := [[1]]\nfor(row : a) { add(row) }\nprint(a.stringify())', '[[1,9]]\n'),
 ('bare nested-object argument alias', 'fn(change(x)) { x["v"] = 9 }\na := [{"v":1}]\nfor(row : a) { change(row) }\nprint(a.stringify())', '[{"v":9}]\n'),
 ('bare ordinary aggregate remains a value', 'fn(add(x)) { x.push(9) }\na := [1]\nadd(a); print(a.stringify())', '[1]\n'),
 ('member iteration snapshot growth', 'a := [{"v":2}]\nfor(row : a) { f := x => row.v; row["v"] = 7; a.push({"v":9}); print(f(0)) }', '2\n'),
 ('negative index', 'f := x => x[-1]\nprint(f([2,7]))', None),
 ('effectful Boolean index', 'fn(key()) { print("key"); return true }\nf := x => x[key()]\nprint(f([1]))', None),
 ('index type error', 'f := x => x[true]\nprint(f([2]))', None),
 ('missing capture', 'f := x => unknown_name\nprint(f(1))', None),
 ('arity error', 'f := x => x\nprint(f())', None),
 ('bounds error', 'f := x => x[99]\nprint(f([1]))', None),
 ('null indexed', 'f := x => x[0]\nprint(f(null))', None),
]
# B5: stored and returned arrays/objects share references. Iteration
# snapshots membership, so mutation of an existing member remains visible.
sharing_output = {
 'indexed returned location': '[[1,2],[9]]\n',
 'mapped aggregate independence': '[[3],[2]]\n[[3],[2]]\n',
 'filter aggregate independence': '[[3],[2]]\n[[3],[2]]\n',
 'group aggregate independence': '[{"k":3},{"k":2}]\n{"1":[{"k":3}],"2":[{"k":2}]}\n',
 'member aggregate independence': '{"v":[1,2]}\n[1,2]\n',
 'sort aggregate independence': '[{"v":3},{"v":9}]\n[{"v":9},{"v":3}]\n',
 'bare ordinary aggregate remains a value': '[1,9]\n',
 'member iteration snapshot growth': '7\n',
}

# D-182. Keep the historical bare-read result independently executable.
lexical_cases = [
 ('local callable shadow', 'fn(x()) { return 8 }\nx := () => 3\ng := x; print(g()); print(x())', '3\n3\n'),
 ('lambda parameter call', 'fn(x()) { return 8 }\nf := x => x()\ng := () => 3\nprint(f(g))', '3\n'),
 ('named parameter shadow', 'fn(x()) { return 8 }\nfn(run_it(x)) { return x }\nprint(run_it(3))', '3\n'),
 ('interpolated lexical call', 'fn(x()) { return [1] }\nx := () => 8\nprint("v=$[x()]")', 'v=8\n'),
 ('shadowed async declaration', 'fn[async](x()) { return 8 }\nx := () => 3\nprint(type(x)); print(x())',
  'function\n3\n'),
 ('named function fallback', 'fn(x()) { return 8 }\nf := y => x()\nprint(f(3))', '8\n'),
 ('local argument order', 'fn(x()) { return 8 }\nn := 0\nfn(arg()) { n += 1; return n }\nx := (a,b) => a*10+b\nprint(x(arg(),arg())); print(n)', '12\n2\n'),
 ('non-callable local shadow', 'fn(x()) { return 8 }\nfn(arg()) { print("argument"); return 1 }\nx := 3\nprint(x(arg()))', None),
]

errors = {
 'non-callable local shadow': 'selector.f:4:7: undefined callable: x',
 'member zero division': 'division by zero',
 'member zero modulo': 'modulo by zero',
 'member missing operand': 'unknown value or malformed expression: missing',
 'member missing arithmetic': 'value has no member: missing',
 'missing member': 'value has no member: missing',
 'null member': 'value has no member: v',
 'negative index': "JSON array indices must be non-negative integers in 'x[-1]'",
 'effectful Boolean index': 'selector.f:2:11: not compiled for the VM (type:index)',
 'index type error': "JSON array indices must be non-negative integers in 'x[true]'",
 'missing capture': 'unknown value or malformed expression: unknown_name',
 'arity error': 'lambda argument count mismatch',
 'bounds error': "JSON array index 99 is out of range in 'x[99]'",
 'null indexed': 'invalid index',
}
with tempfile.TemporaryDirectory() as directory:
 path = Path(directory) / 'selector.f'
 for name,source,expected in lexical_cases + cases:
  path.write_text(source+'\n',encoding='utf-8')
  candidate = subprocess.run([binary,str(path)],capture_output=True,text=True,encoding="utf-8")
  if name == 'named precedence' or name in sharing_output:
   legacy_expected = 'function\n' if name == 'named precedence' else expected
   legacy = subprocess.run([binary,str(path)],capture_output=True,text=True,encoding="utf-8",
                           env=dict(os.environ, NIFT_VM='0'))
   if (legacy.returncode,legacy.stdout,legacy.stderr) != (0,legacy_expected,''):
    raise SystemExit(f'FAIL historical {name}: {legacy}')
   expected = sharing_output.get(name, expected)
  elif baseline and (name,source,expected) not in lexical_cases:
   original = subprocess.run([str(Path(baseline).resolve()),str(path)],capture_output=True,text=True,encoding="utf-8")
   if (candidate.returncode,candidate.stdout,candidate.stderr) != (original.returncode,original.stdout,original.stderr):
    raise SystemExit(f'FAIL A/B {name}: {candidate} != {original}')
  if name == 'non-callable local shadow' and (candidate.returncode != 1 or candidate.stdout != 'argument\n'):
   raise SystemExit(f'FAIL argument evaluation before non-callable error: {candidate}')
  if name == 'effectful Boolean index' and (candidate.stdout or 'the program was not executed' not in candidate.stderr):
   raise SystemExit(f'FAIL effectful index must refuse before execution: {candidate}')
  if expected is None and (candidate.returncode == 0 or errors[name] not in candidate.stderr):
   raise SystemExit(f'FAIL error contract {name}: {candidate}')
  if expected is not None and (candidate.returncode or candidate.stdout != expected):
   raise SystemExit(f'FAIL {name}: {candidate.stdout!r} {candidate.stderr}')
 print(f'PASS {len(cases) + len(lexical_cases)} selector/capture/location/error contracts'+(' with exact A/B diagnostics' if baseline else ''))
