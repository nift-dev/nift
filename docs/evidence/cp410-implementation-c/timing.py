from pathlib import Path
import subprocess,json,time,resource,statistics,os,tempfile
p=Path(__file__).parent.resolve();old=Path('.build/cp410-shell').resolve();wave=Path('.build/cp410-implementation').resolve();rows=[];cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu});n=10000;payload=b'x'*128
for op in ['metadata','move','copy']:
 code=(old/f'{op}-1000/source.f').read_text()
 for repeat in range(6):
  for label in (['accepted','recipe','native'] if repeat%2==0 else ['native','recipe','accepted']):
   with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    for d in ['files','moved','copied']:(root/d).mkdir()
    names=[f'files/obj-{i:06}-target.dat' for i in range(n)]
    for name in names:(root/name).write_bytes(payload)
    (root/'targets.txt').write_text('\n'.join(names)+'\n');binary=p/'accepted-nift' if label=='accepted' else (old/'secondary-control' if label=='native' else p/'nift');f=p/'fs-context-rss.time';before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic();r=subprocess.run(['/usr/bin/time','-f','%M','-o',str(f),str(binary),op if label=='native' else '-'],input=code,cwd=root,text=True,capture_output=True,check=True);wall=time.monotonic()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.stdout==('1280000\n' if op=='metadata' else 'OK\n')
    if op!='metadata':assert all((root/('moved' if op=='move' else 'copied')/Path(name).name).read_bytes()==payload for name in names)
    rows.append(dict(operation=op,label=label,repeat=repeat,n=n,wall=wall,cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,rss_kib=int(f.read_text()),affinity=cpu,scope='same original success fixture; 37 side-effect/error/source contracts checked separately'));(p/'timing.json').write_text(json.dumps(rows,indent=2)+'\n')
  print(op,repeat,'PASS',flush=True)
summary={op:{label:{k:statistics.median(r[k] for r in rows if r['operation']==op and r['label']==label) for k in ['wall','cpu','rss_kib']} for label in ['accepted','recipe','native']} for op in ['metadata','move','copy']};(p/'timing-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary,flush=True)
