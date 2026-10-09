from pathlib import Path
import subprocess,json,re
p=Path('.build/cp410-capture-frame').resolve();root=p/'additional';root.mkdir(exist_ok=True);(root/'module.f').write_text('v := 7\nfn(mk()) { return x => x+v }\nexport(mk)\n');rows=[]
cases={
'root-object-path': 'v := {"longkey":[{"field":1}]}; rows := v["longkey"]; print([2,1].sort_by(x => x*rows[0].field).stringify())',
'root-path': 'a := [{"longkey":1}]; for(row : a) { f := x => row.longkey; a.push({"longkey":9}); print(f(0)) }',
'parameter-name-collision':'x := 99; f := x => x; print(f(3))',
'shadowed-visible-name':'v := 1; if(true) { v := 2; f := x => v; print(f(0)) }',
'module-context':'import("./module.f"); v := 100; f := mk(); print(f(1))',
'parameter-shadowed-timer':'x := timer(); f := async x => x; r := f(3); print(await r)',
'escaping-factory':'saved := []; fn(mk()) { f := x => x; saved.push(f); return f }; print([2,1].sort_by(mk()).stringify()); f := saved[0]; g := saved[1]; print(f(8)); print(g(9)); print(f == g)'
}
for name,code in cases.items():
 file=root/(name+'.f');file.write_text(code);q=subprocess.run([str(p/'phases/nift-counts'),str(file)],capture_output=True,text=True);baseline=subprocess.run([str(Path('.build/cp410-shell/baseline-nift').resolve()),str(file)],capture_output=True,text=True);assert (q.returncode,q.stdout)==(baseline.returncode,baseline.stdout)
 groups={label:{k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)} for label,line in re.findall(r'bindings (\w+) (.*)',q.stderr)}
 rows.append({'name':name,'source':code,'bindings':groups,'returncode':baseline.returncode,'stdout':baseline.stdout,'stderr':baseline.stderr});print(name,groups,baseline.stdout,flush=True)
Path('docs/evidence/cp410-capture-frame/additional-counts.json').write_text(json.dumps(rows,indent=2)+'\n')
