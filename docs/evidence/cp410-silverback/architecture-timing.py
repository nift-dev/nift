from pathlib import Path
import subprocess,json,time,resource,statistics,os,hashlib
p=Path(__file__).parent.resolve();fixtures=Path('.build/cp410-shell').resolve();wave=Path('.build/cp410-wave3').resolve();rows=[];cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu})
def run(name,root,commands,source=None,reps=6):
 expected=None
 for rep in range(reps):
  labels=list(commands);labels=labels if rep%2==0 else list(reversed(labels))
  for label in labels:
   before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic();f=p/'architecture-rss.time';r=subprocess.run(['/usr/bin/time','-f','%M','-o',str(f),*commands[label]],cwd=root,input=source,capture_output=True,text=True,check=True);wall=time.monotonic()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN)
   if expected is None:expected=r.stdout
   assert r.stdout==expected
   rows.append(dict(name=name,label=label,repeat=rep,wall=wall,cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,rss_kib=int(f.read_text()),output_sha256=hashlib.sha256(r.stdout.encode()).hexdigest(),affinity=cpu));(p/'architecture-timing.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(name,'COMPLETE',flush=True)
for shape in ['official-traverse-100000','deep-100000','mixed-100000']:
 run(shape,fixtures/shape,{'current':[str(wave/'string-nift'),'-e','print(ls("files/**/*.dat").size())'],'canonical-native':[str(fixtures/'native-optimized')],'cached-prefix-native':[str(p/'native-prefix-cache')]})
source=json.loads((p/'string-direct-sources.json').read_text())['utf8-large'];run('string-large-pure',p,{'current':[str(wave/'string-nift'),'-'],'large-ast-only':[str(p/'large-ast-only-nift'),'-'],'prepared-values':[str(p/'string-direct-large-nift'),'-']},source,8)
summary={}
for row in rows:
 name=row['name'];label=row['label'];summary.setdefault(name,{})[label]={k:statistics.median(r[k] for r in rows if r['name']==name and r['label']==label) for k in ['wall','cpu','rss_kib']}
(p/'architecture-timing-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print('COMPLETE',flush=True)
