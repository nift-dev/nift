from pathlib import Path
import json,subprocess,time,resource,statistics,os
p=Path('.build/cp410-capture-frame').resolve();binary=str(Path('.build/cp410-shell/baseline-nift').resolve());rows=[];cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu})
for x in json.loads(Path('docs/evidence/cp410-capture-frame/capture-counts.json').read_text()):
 for rep in range(3):
  f=p/'core-resource.time';u=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.monotonic();r=subprocess.run(['/usr/bin/time','-f','%M','-o',str(f),binary,x['source']],capture_output=True,text=True,check=True);elapsed=time.monotonic()-t;v=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.stdout==f'1\n{x["n"]}\n';rows.append({k:x[k] for k in ('n','unused','kind')}|{'rep':rep,'cpu_affinity':cpu,'cpu':v.ru_utime+v.ru_stime-u.ru_utime-u.ru_stime,'wall':elapsed,'rss_kib':int(f.read_text())})
 Path('docs/evidence/cp410-capture-frame/core-timing.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(x['n'],x['unused'],x['kind'],'PASS',flush=True)
print('COMPLETE 120 whole-core CPU/wall/RSS samples')
