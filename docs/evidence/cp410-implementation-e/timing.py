from pathlib import Path
import subprocess,json,time,resource,statistics,os,hashlib
p=Path(__file__).resolve().parent;source=json.loads(Path('.build/cp410-silverback/string-direct-sources.json').read_text())['utf8-large'];rows=[];cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu});expected=None
binaries={'accepted':Path('.build/cp410-implementation/filesystem/nift').resolve(),'canonical-assignment':p/'prototype/nift','native-floor':p/'native-control'}
for repeat in range(6):
 for label in (list(binaries) if repeat%2==0 else list(reversed(binaries))):
  f=p/'rss.time';before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic();q=subprocess.run(['/usr/bin/time','-f','%M','-o',str(f),str(binaries[label]),*(['-'] if label!='native-floor' else [])],input=source if label!='native-floor' else None,capture_output=True,text=True,check=True);wall=time.monotonic()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN)
  if expected is None:expected=q.stdout
  assert q.stdout==expected and not q.stderr,(label,q.stderr)
  rows.append(dict(label=label,repeat=repeat,wall=wall,cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,rss_kib=int(f.read_text()),affinity=cpu,binary_sha256=hashlib.sha256(binaries[label].read_bytes()).hexdigest(),source_sha256=hashlib.sha256(source.encode()).hexdigest(),scope='whole process; same UTF-8 bytes and 1000 repetitions; no concurrent campaign compilation/profile'))
  (p/'timing.json').write_text(json.dumps(rows,indent=2)+'\n')
 print('PASS balanced triple',repeat,flush=True)
summary={label:{k:statistics.median(r[k] for r in rows if r['label']==label) for k in ['wall','cpu','rss_kib']} for label in binaries};(p/'timing-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary,flush=True)
