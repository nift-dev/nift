from pathlib import Path
import json,subprocess,time,shutil
p=Path('.build/cp410-shell').resolve();rows=[]
payload='0123456789abcdef'*7+'0123456789abcde\n'
for n in (1,1000,10000,100000):
 for label in ('direct','atomic','fsync','nift'):
  root=p/f'write-{label}-{n}';shutil.rmtree(root,ignore_errors=True);(root/'files').mkdir(parents=True,exist_ok=True)
  names=[f'files/out-{i}' for i in range(n)];(root/'targets.txt').write_text('\n'.join(names)+'\n')
  code='paths := open("targets.txt").trim().split("\\n"); for(p : paths) { f := file(p); f.open("w"); f.write('+json.dumps(payload)+'); f.save(); f.close() }; print("OK")'
  (root/'source.f').write_text(code)
  cmd=[str(p/'baseline-nift'),'-e',code] if label=='nift' else [str(p/'write-control'),label,str(n)]
  resource=p/f'write-{label}-{n}.time';t=time.monotonic();r=subprocess.run(['/usr/bin/time','-f','%U %S %M','-o',str(resource),*cmd],cwd=root,capture_output=True,text=True,check=True)
  assert r.stdout.strip()=='OK',r.stdout
  elapsed=time.monotonic()-t
  assert all((root/x).read_bytes()==payload.encode() for x in names)
  assert not list((root/'files').glob('*.tmp'))
  row={'label':label,'n':n,'wall':elapsed,'user_system_rss':resource.read_text().strip()};rows.append(row);print(row,flush=True)
Path('docs/evidence/cp410-shell/write-baseline.json').write_text(json.dumps(rows,indent=2)+'\n')
