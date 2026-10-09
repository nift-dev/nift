from pathlib import Path
import subprocess,json,time
p=Path('.build/cp410-shell').resolve(); rows=[]
for n in (1000,10000,100000):
 root=p/('flat-'+str(n));code=(root/'glob.f').read_text()
 for label,cmd in [('baseline',[str(p/'baseline-nift'),'-e',code]),('native',[str(p/'native')])]:
  for rep in range(3):
   resource=p/f'{label}-{n}-{rep}.time';t=time.monotonic()
   r=subprocess.run(['/usr/bin/time','-f','%U %S %M','-o',str(resource),*cmd],cwd=root,capture_output=True,text=True,check=True)
   assert r.stdout.strip()==str(n+max(20000 if n==100000 else n//5,1)),r.stdout
   row={'label':label,'n':n,'repeat':rep,'wall':time.monotonic()-t,'user_system_rss':resource.read_text().strip()};rows.append(row);print(row,flush=True)
Path('docs/evidence/cp410-shell/traversal-baseline.json').write_text(json.dumps(rows,indent=2)+'\n')
