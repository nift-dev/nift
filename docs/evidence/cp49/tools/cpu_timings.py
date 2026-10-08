import json,subprocess,time,statistics,resource,random
from pathlib import Path
b=Path(__file__).resolve().parents[4]/'.build/cp49';rows=json.loads((b/'probes.json').read_text());out=[]
for name in sorted({r['name'] for r in rows}):
 group=[r for r in rows if r['name']==name];random.Random(49).shuffle(group)
 for r in group:
  samples=[]
  for repeat in range(4):
   before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();p=subprocess.run([str(Path('nift').resolve()),r['path']],capture_output=True,text=True,check=True);wall=(time.perf_counter()-t)*1000;after=resource.getrusage(resource.RUSAGE_CHILDREN);cpu=(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime)*1000;lines=p.stdout.splitlines();assert '\n'.join(lines[:-1])==r['expected']
   if repeat:samples.append(dict(cpu_ms=cpu,wall_ms=wall,phase_ms=float(lines[-1])))
  out.append(dict(name=name,n=r['n'],samples=samples,median_cpu_ms=statistics.median(x['cpu_ms'] for x in samples),median_wall_ms=statistics.median(x['wall_ms'] for x in samples),median_phase_ms=statistics.median(x['phase_ms'] for x in samples)));(b/'cpu-timings.json').write_text(json.dumps(out,indent=2)+'\n');print('CPU',name,r['n'],flush=True)
