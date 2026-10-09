from pathlib import Path
import subprocess,json,time,resource,statistics,os
p=Path(__file__).parent.resolve();fixtures=Path('.build/cp410-shell').resolve();rows=[];cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu})
def pair(name,root,code,labels,reps=6,reset=None):
 expected=None
 for rep in range(reps):
  for label in (labels if rep%2==0 else list(reversed(labels))):
   if reset:reset()
   f=p/'acceptance-resource.time';before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.monotonic();r=subprocess.run(['/usr/bin/time','-f','%M','-o',str(f),str(p/(label+'-nift')),'-e',code],cwd=root,text=True,capture_output=True,check=True);elapsed=time.monotonic()-t;after=resource.getrusage(resource.RUSAGE_CHILDREN)
   if expected is None:expected=r.stdout
   assert r.stdout==expected
   rows.append(dict(name=name,repeat=rep,label=label,wall=elapsed,cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,rss_kib=int(f.read_text()),cpu_affinity=cpu));(p/'acceptance-timing.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(name,{l:{k:statistics.median(x[k] for x in rows if x['name']==name and x['label']==l) for k in ['wall','cpu','rss_kib']} for l in labels},flush=True)
for shape in ['official-traverse-100000','deep-100000','mixed-100000']:
 root=fixtures/shape;pair(shape,root,'print(ls("files/**/*.dat").size())',['baseline','string'])
for n in [5000,50000]:pair('string-small-'+str(n),p,f's := "alpha"; i := 0; while(i < {n}) {{ s="alpha".to_upper().replace("A","a"); i+=1 }}; print(s)',['baseline','string'],12)
text='é_λ_Z'*2048;pair('string-large',p,f's := ""; i := 0; while(i < 1000) {{ s={json.dumps(text,ensure_ascii=False)}.to_upper().replace("_","__"); i+=1 }}; print(s)',['baseline','string'])
for n in [1,1000,10000,100000]:
 root=fixtures/f'official-create-small-{n}';names=(root/'targets.txt').read_text().splitlines();code=(root/'source.f').read_text()
 def reset():
  for name in names:(root/name).unlink(missing_ok=True)
 pair('closed-capacity-'+str(n),root,code,['string','close-capacity'],4,reset)
probes=json.loads(Path('.build/cp410-wave3/../cp410/probes.json').read_text());cases=[x for x in probes if x['n']==8000 and x['name'] in ['sort-identity','call-scalar','call-closure','call-callback','loops','bfs','map-set','json-mutate']]
for x in cases:
 code=Path(x['path']).read_text();pair('control-'+x['name'],p,code,['baseline','string'],12)
print('COMPLETE isolated acceptance timings',flush=True)
