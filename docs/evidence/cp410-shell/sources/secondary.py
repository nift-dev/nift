from pathlib import Path
import subprocess,json,time
p=Path('.build/cp410-shell').resolve();payload='0123456789abcdef'*7+'0123456789abcde\n';rows=[]
for n in (1000,10000):
 for op in ('metadata','move','copy','concat','string'):
  root=p/f'{op}-{n}';(root/'files').mkdir(parents=True,exist_ok=True)
  for d in ('copied','moved'):(root/d).mkdir(exist_ok=True)
  names=[f'files/obj-{i:06}.dat' for i in range(n)]
  for name in names:(root/name).write_bytes(payload.encode())
  (root/'targets.txt').write_text('\n'.join(names)+'\n')
  prefix='paths := open("targets.txt").trim().split("\\n"); '
  codes={'metadata':prefix+'total := 0; for(p : paths) { total += stat(p).size }; print(total)', 'move':prefix+'for(p : paths) { move(p,"moved/" + p.split("/").last()) }; print("OK")','copy':prefix+'for(p : paths) { copy(p,"copied/" + p.split("/").last()) }; print("OK")','concat':prefix+'dest := file("output"); dest.open("w"); for(p : paths) { dest.write(open(p)) }; dest.save(); dest.close(); print("OK")','string':f's := "alpha"; i := 0; while(i < {n}) {{ s="alpha".to_upper().replace("A","a"); i+=1 }}; print(s)'}
  code=codes[op];(root/'source.f').write_text(code);t=time.monotonic();r=subprocess.run([str(p/'baseline-nift'),'-e',code],cwd=root,capture_output=True,text=True,check=True);elapsed=time.monotonic()-t
  assert r.stdout.strip()==(str(n*128) if op=='metadata' else 'aLPHa' if op=='string' else 'OK')
  if op=='concat':assert (root/'output').read_bytes()==payload.encode()*n
  for name in names:
   if op=='move':assert not(root/name).exists();assert(root/'moved'/Path(name).name).read_bytes()==payload.encode()
   if op=='copy':assert(root/'copied'/Path(name).name).read_bytes()==payload.encode()
  row={'n':n,'op':op,'wall':elapsed};rows.append(row);print(row,flush=True)
Path('docs/evidence/cp410-shell/secondary-baseline.json').write_text(json.dumps(rows,indent=2)+'\n')
