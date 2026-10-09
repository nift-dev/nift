from pathlib import Path
import subprocess,json,resource,statistics,os
p=Path('.build/cp410-implementation/traversal').resolve();fixtures=Path('.build/cp410-shell').resolve();commands={'accepted':[str(Path('.build/cp410-implementation/snapshot-nift').resolve()),'-e','print(ls("files/**/*.dat").size())'],'shared':[str(p/'nift'),'-e','print(ls("files/**/*.dat").size())'],'native-prefix':[str(Path('.build/cp410-silverback/native-prefix-cache').resolve())]};rows=[]
cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu})
for shape in ('official-traverse-100000','deep-100000','mixed-100000'):
 expected=None
 for rep in range(6):
  for label in (list(commands) if rep%2==0 else list(reversed(commands))):
   before=resource.getrusage(resource.RUSAGE_CHILDREN);q=subprocess.run(['/usr/bin/time','-f','%M','-o',str(p/'rss.time'),*commands[label]],cwd=fixtures/shape,capture_output=True,text=True,check=True);after=resource.getrusage(resource.RUSAGE_CHILDREN)
   if expected is None:expected=q.stdout
   assert q.stdout==expected;rows.append(dict(shape=shape,rep=rep,label=label,cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,rss_kib=int((p/'rss.time').read_text()),affinity=cpu))
 (p/'timing.json').write_text(json.dumps(rows,indent=2)+'\n');print(shape,{label:{key:statistics.median(r[key] for r in rows if r['shape']==shape and r['label']==label) for key in ('cpu','rss_kib')} for label in commands},flush=True)
print('COMPLETE isolated six balanced triples per shape')
