from pathlib import Path
import json,subprocess,time,shutil
p=Path('.build/cp410-shell').resolve();rows=[];payload='0123456789abcdef'*7+'0123456789abcde\n'
for op,n in [('save',1000),('save',10000),('save',100000),('string',5000),('string',50000)]:
 for rep in range(4):
  for label in (('baseline','dispatch') if rep%2==0 else ('dispatch','baseline')):
   root=p/f'dispatch-{op}-{n}';shutil.rmtree(root,ignore_errors=True);root.mkdir()
   if op=='save':
    (root/'files').mkdir();names=[f'files/out-{i}' for i in range(n)];(root/'targets.txt').write_text('\n'.join(names)+'\n');code='paths := open("targets.txt").trim().split("\\n"); for(p : paths) { f := file(p); f.open("w"); f.write('+json.dumps(payload)+'); f.save(); f.close() }; print("OK")'
   else:code=f's := "alpha"; i := 0; while(i < {n}) {{ s="alpha".to_upper().replace("A","a"); i+=1 }}; print(s)'
   resource=p/'resource.time';t=time.monotonic();r=subprocess.run(['/usr/bin/time','-f','%U %S %M','-o',str(resource),str(p/(label+'-nift')),'-e',code],cwd=root,capture_output=True,text=True,check=True);wall=time.monotonic()-t
   assert r.stdout.strip()==('OK' if op=='save' else 'aLPHa')
   if op=='save':assert all((root/name).read_bytes()==payload.encode() for name in names)
   row={'op':op,'n':n,'repeat':rep,'label':label,'wall':wall,'user_system_rss':resource.read_text().strip()};rows.append(row);print(row,flush=True)
Path('docs/evidence/cp410-shell/dispatch-paired.json').write_text(json.dumps(rows,indent=2)+'\n')
