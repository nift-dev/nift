from pathlib import Path
import subprocess,json,time
p=Path('.build/cp410-shell').resolve();rows=[]
for shape in ('flat','deep','mixed'):
 for n in (1000,10000,100000):
  root=p/f'{shape}-{n}'
  for pattern,expected in [('files/**/*.dat',n+max(20000 if n==100000 else n//5,1) if shape=='flat' else n),('files/**/*.missing',0),('files/**/obj-*000.dat',None)]:
   code='paths := ls('+json.dumps(pattern)+'); print(paths.size())'
   outputs=[]
   for rep in range(3):
    for label in (('baseline','prototype') if rep%2==0 else ('prototype','baseline')):
     resource=p/'resource.time';t=time.monotonic();r=subprocess.run(['/usr/bin/time','-f','%U %S %M','-o',str(resource),str(p/(label+'-nift')),'-e',code],cwd=root,capture_output=True,text=True,check=True)
     wall=time.monotonic()-t;outputs.append(r.stdout)
     if expected is not None:assert r.stdout.strip()==str(expected),r.stdout
     row={'shape':shape,'n':n,'pattern':pattern,'repeat':rep,'label':label,'wall':wall,'user_system_rss':resource.read_text().strip(),'stdout':r.stdout.strip()};rows.append(row);print(row,flush=True)
   assert len(set(outputs))==1
Path('docs/evidence/cp410-shell/traversal-paired.json').write_text(json.dumps(rows,indent=2)+'\n')
