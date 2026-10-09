from pathlib import Path
import json,subprocess,time,statistics,resource
p=Path('.build/cp410-shell').resolve();rows=[]
probes=json.loads(Path('.build/cp410/probes.json').read_text());cases=[x for x in probes if x['n']==8000 and x['name'] in ('loops','call-scalar','call-closure','call-callback','map-set','json-parse-convert','json-traverse','json-mutate','json-serialize','bfs','sort-identity')]
for x in cases:
 expected=None
 for rep in range(10):
  for label in (('baseline','candidate') if rep%2==0 else ('candidate','baseline')):
   u=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.monotonic();r=subprocess.run([str(p/(label+'-nift')),x['path']],capture_output=True,text=True,check=True);elapsed=time.monotonic()-t;v=resource.getrusage(resource.RUSAGE_CHILDREN)
   if expected is None:expected=r.stdout
   assert r.stdout==expected
   if rep:rows.append({'name':x['name'],'n':8000,'rep':rep,'label':label,'cpu':v.ru_utime+v.ru_stime-u.ru_utime-u.ru_stime,'wall':elapsed})
 print(x['name'],'PASS',flush=True)
 Path('docs/evidence/cp410-shell/control-timing.json').write_text(json.dumps(rows,indent=2)+'\n')
# Exact official create-small fixture/API; fresh target files, keepers retained.
rows=[];payload=('0123456789abcdef'*7+'0123456789abcde\n').encode()
for n in (1,1000,10000,100000):
 root=p/f'official-create-small-{n}';names=(root/'targets.txt').read_text().splitlines();code=(root/'source.f').read_text()
 for rep in range(3):
  labels=('baseline','candidate','direct','atomic') if rep%2==0 else ('atomic','direct','candidate','baseline')
  for label in labels:
   for name in names:(root/name).unlink(missing_ok=True)
   cmd=[str(p/(label+'-nift')),'-e',code] if label in ('baseline','candidate') else [str(p/'write-control-exact'),label,str(n)]
   f=p/'save-resource.time';t=time.monotonic();r=subprocess.run(['/usr/bin/time','-f','%U %S %M','-o',str(f),*cmd],cwd=root,capture_output=True,text=True,check=True);elapsed=time.monotonic()-t;assert r.stdout=='OK\n';assert all((root/name).read_bytes()==payload for name in names)
   u,s,rss=f.read_text().split();rows.append({'n':n,'rep':rep,'label':label,'wall':elapsed,'cpu':float(u)+float(s),'rss_kib':int(rss)})
  print('exact-save',n,rep,'PASS',flush=True)
 Path('docs/evidence/cp410-shell/final-save-timing.json').write_text(json.dumps(rows,indent=2)+'\n')
print('COMPLETE isolated independent and exact-save timing')
