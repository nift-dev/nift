from pathlib import Path
import subprocess,json,time
p=Path('.build/cp410-shell').resolve();rows=[]
for n in (1000,10000,100000):
 root=p/f'official-traverse-{n}';code=(root/'source.f').read_text();expected=n+max(20000 if n==100000 else n//5,1)
 for rep in range(4):
  labels=['baseline','prototype','unsorted','native'];labels=labels if rep%2==0 else labels[::-1]
  for label in labels:
   cmd=[str(p/'native')] if label=='native' else [str(p/(label+'-nift')),'-e',code]
   resource=p/'resource.time';t=time.monotonic();r=subprocess.run(['/usr/bin/time','-f','%U %S %M','-o',str(resource),*cmd],cwd=root,capture_output=True,text=True,check=True);wall=time.monotonic()-t;assert r.stdout.strip()==str(expected)
   row={'n':n,'repeat':rep,'label':label,'wall':wall,'user_system_rss':resource.read_text().strip()};rows.append(row);print(row,flush=True)
Path('docs/evidence/cp410-shell/official-traversal-paired.json').write_text(json.dumps(rows,indent=2)+'\n')
