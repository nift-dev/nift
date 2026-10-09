from pathlib import Path
import json,subprocess,re
p=Path('.build/cp410-capture-frame');binary=str((p/'phases/nift-counts').resolve());rows=[]
for n in (2000,4000,8000,16000):
 for unused in (0,5,16,50,100):
  for kind in ('identity','captured'):
   root=p/'cases'/f'n{n:06}'/f'u{unused:03}'/f'{kind:_<12}';root.mkdir(parents=True,exist_ok=True);path=root/'official-equivalent.f'
   prefix=''.join(f'unused{k:03} := {k}\n' for k in range(unused))+('offset := 1\n' if kind=='captured' else '')
   setup=f'a := []\ni := 1\nwhile(i <= {n}) {{ a.push({n}-i+1); i += 1 }}\n';selector='x => x+offset' if kind=='captured' else 'x => x';source=prefix+setup+'a = a.sort_by('+selector+f')\nprint(a[0]); print(a[{n}-1])\n';path.write_text(source)
   r=subprocess.run([binary,str(path.resolve())],capture_output=True,text=True,check=True);assert r.stdout==f'1\n{n}\n'
   groups={}
   for label,line in re.findall(r'bindings (\w+) (.*)',r.stderr):groups[label]={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
   phases={m[0]:dict(calls=int(m[1]),allocations=int(m[2]),bytes=int(m[3]),frees=int(m[4])) for m in re.findall(r'phase (\w+) calls=(\d+) allocations=(\d+) bytes=(\d+) frees=(\d+)',r.stderr)}
   assert groups and phases
   expected=n*(unused+4+(kind=='captured'));assert groups['capture']['entries']==expected and groups['frame']['entries']==expected,(n,unused,kind,groups)
   row={'n':n,'unused':unused,'kind':kind,'source':str(path.resolve()),'bindings':groups,'phases':phases};rows.append(row);Path('docs/evidence/cp410-capture-frame/capture-counts.json').write_text(json.dumps(rows,indent=2)+'\n');print(n,unused,kind,groups,flush=True)
print('PASS 40 capture/frame slope profiles')
