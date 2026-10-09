from pathlib import Path
import subprocess,json,time
p=Path('.build/cp410-shell').resolve();short=Path((p/'short-root.txt').read_text());rows=[]
for shape,root,n in [('flat',p/'flat-100000',120000),('deep',p/'deep-100000',100000),('mixed',p/'mixed-100000',100000),('short',short,120000)]:
 for pattern in ('files/**/*.dat','files/**/*.missing','files/**/obj-*000.dat'):
  code='paths := ls('+json.dumps(pattern)+'); print(paths.size())';outputs=[]
  for rep in range(4):
   labels=['baseline','prototype','unsorted']
   if pattern=='files/**/*.dat':labels+=['native']
   if rep%2:labels.reverse()
   for label in labels:
    cmd=[str(p/'native')] if label=='native' else [str(p/(label+'-nift')),'-e',code]
    resource=p/'resource.time';t=time.monotonic();r=subprocess.run(['/usr/bin/time','-f','%U %S %M','-o',str(resource),*cmd],cwd=root,capture_output=True,text=True,check=True);wall=time.monotonic()-t;outputs.append(r.stdout)
    if pattern=='files/**/*.dat':assert r.stdout.strip()==str(n)
    row={'shape':shape,'pattern':pattern,'repeat':rep,'label':label,'wall':wall,'user_system_rss':resource.read_text().strip()};rows.append(row);print(row,flush=True)
  assert len(set(outputs))==1
Path('docs/evidence/cp410-shell/refined-traversal.json').write_text(json.dumps(rows,indent=2)+'\n')
