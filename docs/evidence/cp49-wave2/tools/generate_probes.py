"""Independent local shape/component controls, separate from official workloads."""
import json,pathlib,subprocess
b=pathlib.Path('.build/cp49-wave2/probes');b.mkdir(parents=True,exist_ok=True);rows=[x for x in json.load(open('.build/cp49-wave2/probes.json')) if not (x['name'].startswith('json-') or x['name'].startswith('bfs-'))]
def add(name,code):
 p=b/(name+'-2000.f');p.write_text(code+'\nprobe_timer := timer(); probe_timer.start(); probe_timer.stop(); print(probe_timer.elapsed())\n')
 q=subprocess.run([str(pathlib.Path('.build/cp49-wave2/start-nift').resolve()),str(p.resolve())],capture_output=True,text=True);assert q.returncode==0,(name,q.stderr)
 rows.append(dict(name=name,n=2000,path=str(p.resolve()),expected='\n'.join(q.stdout.splitlines()[:-1])))
for shape in ['wide','deep','mixed']:
 path=str(pathlib.Path(f'.build/cp49-campaign/10/{shape}-2000.json').resolve())
 add('json-'+shape,'a := inject('+json.dumps(path)+'); s := a.stringify(); print(s.length())')
for name,body in [('queue','q := []; i := 0; while(i < 2000) { q.push(i); i += 1 }; i = 0; s := 0; while(i < q.size()) { s += q[i]; i += 1 }; print(s)'),('set-visit','s := set(); i := 0; while(i < 2000) { s.add(i); i += 1 }; i = 0; n := 0; while(i < 2000) { if(s.contains(i)) { n += 1 }; i += 1 }; print(n)'),('map-update','m := map(); i := 0; while(i < 2000) { m.set(i % 37,i); i += 1 }; print(m.size())'),('nested-index','a := []; i := 0; while(i < 2000) { a.push([i,i+1]); i += 1 }; i = 0; s := 0; while(i < 2000) { s += a[i][1]; i += 1 }; print(s)')]:add('bfs-'+name,body)
pathlib.Path('.build/cp49-wave2/probes.json').write_text(json.dumps(rows,indent=2)+'\n')
