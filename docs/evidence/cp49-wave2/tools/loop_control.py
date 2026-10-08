"""Larger loop control, same executable pathname, paired order, no compilers."""
import pathlib,subprocess,shutil,resource,time,json,statistics,hashlib
b=pathlib.Path('.build/cp49-wave2/loop-control');b.mkdir(exist_ok=True,parents=True)
p=b/'loops.f';p.write_text(pathlib.Path('.build/cp49/probes/loops-2000.f').read_text().replace('while(i < 2000)','while(i < 1000000)'))
bins=[pathlib.Path('.build/cp49-wave2/start-nift'),pathlib.Path('.build/cp49-wave2/final-nift')];run=b/'nift';results=[{'sha256':hashlib.sha256(x.read_bytes()).hexdigest(),'samples':[]} for x in bins];expected=None
for rep in range(22):
 for i in ([0,1] if rep%2==0 else [1,0]):
  shutil.copy2(bins[i],run);before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();q=subprocess.run([str(run.resolve()),str(p.resolve())],capture_output=True,text=True);wall=time.perf_counter()-t;after=resource.getrusage(resource.RUSAGE_CHILDREN);assert q.returncode==0,q.stderr;output='\n'.join(q.stdout.splitlines()[:-1]);expected=output if expected is None else expected;assert output==expected
  if rep:results[i]['samples'].append({'cpu_ms':1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime),'wall_ms':1000*wall,'in_process_ms':float(q.stdout.splitlines()[-1])})
for r in results:
 for key in ['cpu_ms','wall_ms','in_process_ms']:r['median_'+key]=statistics.median(x[key] for x in r['samples'])
(b/'results.json').write_text(json.dumps(results,indent=2)+'\n');print([(r['median_cpu_ms'],r['median_in_process_ms']) for r in results])
