from pathlib import Path
import subprocess,tempfile,json,hashlib,resource,time,statistics,os
p=Path(__file__).resolve().parent;source=(p.parents[1]/'cp410-shell/concat-1000/source.f').read_text();payload=next((p.parents[1]/'cp410-shell/concat-1000/files').iterdir()).read_bytes();rows=[]
binaries={'accepted-save':p.parent/'save/recipe-only-nift','owned-write':p/'prototype/nift','native-stream':p.parents[1]/'cp410-shell/secondary-control'}
cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu})
for size,count in [(128,1000),(1048576,32)]:
 data=(payload*((size+len(payload)-1)//len(payload)))[:size];expected=hashlib.sha256(data*count).hexdigest()
 for repeat in range(6):
  for label in (list(binaries) if repeat%2==0 else list(reversed(binaries))):
   with tempfile.TemporaryDirectory(prefix='nift-concat-time-') as directory:
    root=Path(directory);(root/'files').mkdir();names=['files/'+str(i) for i in range(count)]
    for name in names:(root/name).write_bytes(data)
    (root/'targets.txt').write_text(''.join(name+'\n' for name in names));script=root/'source.f';script.write_text(source)
    rss=p/'rss.time';args=['concat'] if label=='native-stream' else [str(script)];before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic()
    q=subprocess.run(['/usr/bin/time','-f','%M','-o',str(rss),str(binaries[label]),*args],cwd=root,capture_output=True,text=True,check=True)
    wall=time.monotonic()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN)
    assert q.stdout=='OK\n' and not q.stderr
    assert hashlib.sha256((root/'output').read_bytes()).hexdigest()==expected
    rows.append(dict(size=size,count=count,repeat=repeat,label=label,cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,wall=wall,rss_kib=int(rss.read_text()),affinity=cpu,binary_sha256=hashlib.sha256(binaries[label].read_bytes()).hexdigest()));(p/'timing.json').write_text(json.dumps(rows,indent=2)+'\n')
  print('PASS',size,repeat,flush=True)
summary={str(size):{label:{key:statistics.median(r[key] for r in rows if r['size']==size and r['label']==label) for key in ['cpu','wall','rss_kib']} for label in binaries} for size in [128,1048576]};(p/'timing-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary,flush=True)
