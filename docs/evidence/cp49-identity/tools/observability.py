"""Explicit current-semantics contract probes; baseline is the accepted wave-1 oracle."""
import pathlib,subprocess,json,os
b=pathlib.Path('.build/cp49-identity/contracts');b.mkdir(parents=True,exist_ok=True)
cases=[
('factory-count','n := 0\nfn(mk()) { n += 1; return x => x }\nprint([3,1,2].sort_by(mk()).stringify()); print(n)\nprint([3,1,2].map(mk()).stringify()); print(n)\nprint([].sort_by(mk()).stringify()); print(n)\nprint([].map(mk()).stringify()); print(n)','[1,2,3]\n3\n[3,1,2]\n4\n[]\n4\n[]\n5\n'),
('factory-order','n := 0\nfn(mk()) { n += 1; print(n); return x => x }\nprint([3,1,2].sort_by(mk(), "asc", mk(), "desc").stringify())','1\n2\n3\n4\n5\n6\n[1,2,3]\n'),
('fresh-identity','f := x => x\ng := x => x\nprint(f == g); print(f == f)','false\ntrue\n'),
('escape-identities','saved := []\nfn(mk()) { f := x => x; saved.push(f); return f }\nprint([2,1].sort_by(mk()).stringify()); print(saved[0] == saved[1])','[1,2]\nfalse\n'),
('capture-mutation','n := 0\nf := x => n++\nprint([3,1,2].sort_by(f).stringify()); print(n)','[3,1,2]\n3\n'),
('factory-capture-change','n := 0\nfn(mk()) { n += 1; return x => n }\nprint([3,1,2].sort_by(mk()).stringify()); print(n)','[3,1,2]\n3\n'),
('dynamic-capture-dependency','fn(read_hidden()) { return hidden }\nfn(mk(hidden)) { return x => read_hidden() }\nf := mk(7)\nhidden := 100\nprint(f(0))','7\n'),
('changing-outer','n := 1\nf := x => x + n\nprint(f(2)); n = 7; print(f(2))','3\n9\n'),
('shadowing','x := 99\nf := x => x\nprint(f(3)); print(x)','3\n99\n'),
('named-precedence','fn(x()) { return 8 }\nf := x => x\nprint(type(f(3)))','function\n'),
('escaped-local-captures','fn(mk(v)) { return x => v }\na := mk(2); b := mk(8); print(a(0)); print(b(0))','2\n8\n'),
('returned-callables','a := [2,7].map(x => (y => x))\nf := a[0]; g := a[1]; print(f(0)); print(g(0)); print(f == g)','2\n7\nfalse\n'),
('root-path-growth','a := [[1]]\nfor(row : a) { f := x => row[0]; a.push([9]); print(f(0)) }','1\n'),
('unused-timer-observable','t := timer()\nf := async x => x\nr := f(3)\nprint(await r)',{'error':'async function capture contains a non-transferable timer'}),
('async-invoke','f := async x => x + 1\nr := f(3); print(await r)','4\n'),
('async-callback-current','f := async x => x + 1\nprint([3,1,2].sort_by(f).stringify())','[1,2,3]\n'),
('throwing-factory','n := 0\nfn(mk()) { n += 1; if(n == 2) { throw error("stop", "user.selector") }; return x => x }\ntry { [3,1,2].sort_by(mk()) } catch(e) { print(e.message) }\nprint(n)','stop\n2\n'),
('bad-key-cleanup','try { [2,1].sort_by(x => [x]) } catch(e) { print("caught") }\nprint([2,1].sort_by(x => x).stringify())',None),
]
# Defining module owns the callable; caller binding cannot replace its private state.
(b/'module.f').write_text('v := 7\nfn(mk()) { return x => x + v }\nexport(mk)\n')
cases.append(('module-ownership','import("./module.f")\nv := 100\nf := mk(); print(f(1)); print([3,1,2].sort_by(f).stringify())','8\n[1,2,3]\n'))
results=[];bins=[str(pathlib.Path('.build/cp49-wave2/start-nift').resolve()),str(pathlib.Path('nift').resolve())]
for name,source,expected in cases:
 p=b/(name+'.f');p.write_text(source+'\n');runs=[subprocess.run([binary,str(p.resolve())],capture_output=True,text=True,timeout=20) for binary in bins]
 a,c=runs;assert (a.returncode,a.stdout,a.stderr)==(c.returncode,c.stdout,c.stderr),(name,a,c)
 if isinstance(expected,dict):assert c.returncode!=0 and expected['error'] in c.stderr,(name,c)
 elif expected is not None:assert c.returncode==0 and c.stdout==expected,(name,c,expected)
 else:assert c.returncode!=0 and 'key must be scalar' in c.stderr,(name,c)
 results.append(dict(name=name,source=source,expected=expected,exit=c.returncode,stdout=c.stdout,stderr=c.stderr,exact_baseline_match=True));print(name,'PASS',flush=True)
(b/'results.json').write_text(json.dumps(results,indent=2)+'\n')
