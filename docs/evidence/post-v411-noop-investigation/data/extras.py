import pathlib,json,os,subprocess,time,collections,re
p=pathlib.Path('/tmp/nift-noop-investigation');os.sched_setaffinity(0,set(json.loads((p/'identity.json').read_text())['cpus']));exe='/home/nick/Repositories/nift/nift/nift';ninja='/tmp/build-systems-tools/ninja/usr/bin/ninja';env=os.environ.copy();rows=[]
for size in [100,1000,5000,10000]:
 d=p/f'fixture-{size}-nift';trace=d/'probe.trace';trace.unlink(missing_ok=True);e=env|{'NIFT_DIAG_FILE':str(p/f'counters-{size}.csv'),'BUILD_TRACE':str(trace)};r=subprocess.run([str(p/'core/nift-diag'),'build'],cwd=d,env=e,capture_output=True,text=True);assert r.returncode==0 and not trace.exists();print('counters',size,flush=True)
 for i in range(5):
  t=time.perf_counter();r=subprocess.run([str(p/'core-stage/nift-stage'),'build'],cwd=d,env=env|{'NIFT_TEST_BUILD_DAG_STATS':'1'},capture_output=True,text=True);wall=(time.perf_counter()-t)*1000;assert r.returncode==0;stages={a:float(b) for a,b in re.findall(r'STAGE (\w+) ([\d.]+)',r.stderr)};rows.append({'size':size,'warmup':i<2,'wall_ms':wall,'stages_ms':stages,'dag':re.search(r'BUILD_DAG [^\n]+',r.stderr)[0]})
(p/'stages.json').write_text(json.dumps(rows,indent=2)+'\n')
r=subprocess.run([ninja,'-d','stats','-j4'],cwd=p/'fixture-10000-ninja',capture_output=True,text=True,check=True);(p/'ninja-stats.txt').write_text(r.stdout+r.stderr)
subprocess.run(['strace','-c','-o',str(p/'strace-main-only-1000-nift.txt'),exe,'build'],cwd=p/'fixture-1000-nift',check=True,stdout=subprocess.DEVNULL)
subprocess.run(['strace','-f','-c','-o',str(p/'strace-10000-make.txt'),'make','-j4'],cwd=p/'fixture-10000-make',check=True,stdout=subprocess.DEVNULL)
for system in ['nift','ninja']:
 cmd=[exe,'build'] if system=='nift' else [ninja,'-j4'];d=p/f'fixture-10000-{system}';subprocess.run(['strace','-f','-e','trace=newfstatat','-o',str(p/f'paths-10000-{system}.txt'),*cmd],cwd=d,check=True,stdout=subprocess.DEVNULL);freq=collections.Counter();fd=0
 for line in (p/f'paths-10000-{system}.txt').open():
  m=re.search(r'newfstatat\([^,]*, "([^"]*)"',line)
  if m:
   if m[1]:freq[m[1]]+=1
   else:fd+=1
 (p/f'path-frequency-10000-{system}.json').write_text(json.dumps({'named_stat_calls':sum(freq.values()),'unique_path_spellings':len(freq),'fd_stat_calls':fd,'top':freq.most_common(30)},indent=2)+'\n');print('paths',system,len(freq),flush=True)
