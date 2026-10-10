import pathlib,sys,json,os,subprocess,time,statistics,resource,hashlib
p=pathlib.Path('/tmp/nift-noop-investigation');sys.path.insert(0,str(p/'suite/generators'));sys.path.insert(0,str(p/'suite/scripts'));from native import render
from certify import dependency_audit
os.environ['NINJA']='/tmp/build-systems-tools/ninja/usr/bin/ninja';os.sched_setaffinity(0,set(json.loads((p/'identity.json').read_text())['cpus']));(p/'baseline.json').rename(p/'baseline-pre-encoding-correction.json');rows=[]
for size in [100,1000,5000,10000]:
 for system in ['make','ninja','nift']:
  d=p/f'fixture-{size}-{system}';m=json.loads((d/'manifest.json').read_text());render(d,m['actions'],4,'build/bin/app.exe');cmd=['/home/nick/Repositories/nift/nift/nift','build'] if system=='nift' else ['make' if system=='make' else os.environ['NINJA'],'-j4'];env=os.environ.copy();env.update(BUILD_TRACE=str(d/'diagnostic.trace'),LC_ALL='C',TZ='UTC');samples=[]
  for i in range(12):
   trace=d/'diagnostic.trace';trace.unlink(missing_ok=True);before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();r=subprocess.run(cmd,cwd=d,env=env,capture_output=True,text=True);wall=time.perf_counter()-t;after=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.returncode==0,(size,system,r.stderr);assert not trace.exists() or trace.read_text()=='',(size,system,'actions')
   samples.append({'warmup':i<2,'wall_ms':wall*1000,'cpu_ms':(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime)*1000})
  assert subprocess.check_output([str(d/'build/bin/app.exe')],text=True)==m['expected_stdout'];audit=dependency_audit(d,system,m)
  row={'size':size,'system':system,'command':cmd,'encoding':'accepted frozen suite2467101 shared rules and no extra Make/Ninja recipes','median_ms':statistics.median(s['wall_ms'] for s in samples[2:]),'min_ms':min(s['wall_ms'] for s in samples[2:]),'max_ms':max(s['wall_ms'] for s in samples[2:]),'zero_actions':True,'compiler_include_audit':audit,'samples':samples};rows.append(row);(p/'baseline.json').write_text(json.dumps(rows,indent=2)+'\n');print(size,system,round(row['median_ms'],3),flush=True)
