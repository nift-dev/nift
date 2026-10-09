from pathlib import Path
import json,subprocess,time,resource,statistics,os
p=Path('.build/cp410-shell').resolve();rows=json.loads(Path('docs/evidence/cp410-shell/long-control-timing.json').read_text());sources=json.loads(Path('.build/cp410/probes.json').read_text());cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu})
records=json.loads(Path('.build/cp49/probes/records-8000.json').read_text());(p/'records-128000.json').write_text(json.dumps(records*16,separators=(',',':')))
for name in ('json-parse-convert','json-mutate','json-serialize','call-scalar','bfs'):
 src=next(x for x in sources if x['name']==name and x['n']==8000);code=Path(src['path']).read_text().replace('8000','128000').replace('/home/nick/Repositories/nift/nift/.build/cp49/probes/records-128000.json',str(p/'records-128000.json'));f=p/(name+'-128000.f');f.write_text(code);cmds={label:[str(p/(label+'-nift')),str(f)] for label in ('baseline','candidate')};expected=subprocess.check_output(cmds['baseline'],text=True)
 for label in cmds:assert subprocess.check_output(cmds[label],text=True)==expected
 for rep in range(12):
  sample={}
  for label in (('baseline','candidate') if rep%2==0 else ('candidate','baseline')):
   a=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.monotonic();r=subprocess.run(cmds[label],capture_output=True,text=True,check=True);wall=time.monotonic()-t;b=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.stdout==expected;sample[label]={'cpu':b.ru_utime+b.ru_stime-a.ru_utime-a.ru_stime,'wall':wall}
  rows.append({'name':name,'n':128000,'rep':rep,'cpu_affinity':cpu,'samples':sample,'paired_cpu_pct':100*(sample['candidate']['cpu']/sample['baseline']['cpu']-1)});Path('docs/evidence/cp410-shell/long-control-timing.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(name,'paired_cpu_pct',statistics.median(x['paired_cpu_pct'] for x in rows if x['name']==name),flush=True)
print('COMPLETE isolated pinned-core long controls')
