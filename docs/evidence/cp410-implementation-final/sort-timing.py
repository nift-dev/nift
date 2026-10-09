from pathlib import Path
import subprocess,json,resource,statistics,os
p=Path('.build/cp410-implementation/final-profile').resolve();binary={'baseline':p.parent/'baseline-nift','final':Path('nift').resolve()};rows=[]
cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu})
for width in (0,100):
 for kind in ('identity','captured'):
  source=Path(f'.build/cp410-capture-frame/cases/n016000/u{width:03}/{kind:_<12}/official-equivalent.f').resolve()
  for rep in range(6):
   for label in (('baseline','final') if rep%2==0 else ('final','baseline')):
    before=resource.getrusage(resource.RUSAGE_CHILDREN);q=subprocess.run(['/usr/bin/time','-f','%M','-o',str(p/'rss.time'),str(binary[label]),str(source)],capture_output=True,text=True,check=True);after=resource.getrusage(resource.RUSAGE_CHILDREN);assert q.stdout=='1\n16000\n';rows.append(dict(width=width,kind=kind,rep=rep,label=label,cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,rss_kib=int((p/'rss.time').read_text())))
  print(width,kind,{label:{'cpu':statistics.median(r['cpu'] for r in rows if (r['width'],r['kind'],r['label'])==(width,kind,label)),'rss':statistics.median(r['rss_kib'] for r in rows if (r['width'],r['kind'],r['label'])==(width,kind,label))} for label in binary},flush=True)
  (p/'sort-timing.json').write_text(json.dumps(rows,indent=2)+'\n')
