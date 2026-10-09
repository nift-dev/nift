from pathlib import Path
import subprocess,json,time,resource,statistics,os,random
p=Path('.build/cp410-shell').resolve();cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu});f=p/'map-set-256000.f';f.write_text((p/'map-set-128000.f').read_text().replace('128000','256000'));rows=[]
for label in ('baseline','candidate'):assert subprocess.check_output([str(p/(label+'-nift')),str(f)],text=True)=='256000\n'
for rep in range(32):
 sample={}
 for label in (('baseline','candidate') if rep%2==0 else ('candidate','baseline')):
  u=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.monotonic();r=subprocess.run([str(p/(label+'-nift')),str(f)],capture_output=True,text=True,check=True);v=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.stdout=='256000\n';sample[label]={'wall':time.monotonic()-t,'cpu':v.ru_utime+v.ru_stime-u.ru_utime-u.ru_stime}
 rows.append({'rep':rep,'n':256000,'cpu_affinity':cpu,'samples':sample,'paired_cpu_pct':100*(sample['candidate']['cpu']/sample['baseline']['cpu']-1)});Path('docs/evidence/cp410-shell/map-stability.json').write_text(json.dumps(rows,indent=2)+'\n')
print('COMPLETE 32 larger balanced map/set pairs',statistics.median(x['paired_cpu_pct'] for x in rows),flush=True)
