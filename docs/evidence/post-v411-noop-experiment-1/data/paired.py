import pathlib,json,os,time,subprocess,statistics,resource,hashlib
p=pathlib.Path('/tmp/nift-status-experiment');fixtures=pathlib.Path('/tmp/nift-noop-investigation');cpus=json.load(open(p/'identity.json'))['cpus'];os.sched_setaffinity(0,set(cpus));binaries={'baseline':str(p/'nift-baseline'),'candidate':'/home/nick/Repositories/nift/nift/nift'};rows=[]
for size in [100,1000,5000,10000]:
 d=fixtures/f'fixture-{size}-nift';m=json.load(open(d/'manifest.json'));before={a['id']:hashlib.sha256((d/a['output']).read_bytes()).hexdigest() for a in m['actions']}
 for i in range(22):
  for name in (['baseline','candidate'] if i%2==0 else ['candidate','baseline']):
   trace=d/'experiment.trace';trace.unlink(missing_ok=True);old=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.perf_counter_ns();r=subprocess.run([binaries[name],'build'],cwd=d,env=os.environ|{'BUILD_TRACE':str(trace)},capture_output=True,text=True);wall=(time.perf_counter_ns()-start)/1e6;new=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.returncode==0 and not trace.exists(),(name,size,r.stderr);rows.append({'size':size,'binary':name,'round':i,'warmup':i<2,'wall_ms':wall,'cpu_ms':(new.ru_utime+new.ru_stime-old.ru_utime-old.ru_stime)*1000})
 assert before=={a['id']:hashlib.sha256((d/a['output']).read_bytes()).hexdigest() for a in m['actions']};assert subprocess.check_output([str(d/'build/bin/app.exe')],text=True)==m['expected_stdout'];(p/'paired.json').write_text(json.dumps(rows,indent=2)+'\n')
 for name in binaries:print(size,name,round(statistics.median(r['wall_ms'] for r in rows if r['size']==size and r['binary']==name and not r['warmup']),3),flush=True)
