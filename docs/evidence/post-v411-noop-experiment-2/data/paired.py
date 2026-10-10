import pathlib,json,os,time,subprocess,statistics,resource,hashlib
p=pathlib.Path('/tmp/nift-path-experiment');fixtures=pathlib.Path('/tmp/nift-noop-investigation');os.sched_setaffinity(0,{0,1,2,3});bins={'experiment1':str(p/'nift-experiment1'),'experiment2':'/home/nick/Repositories/nift/nift/nift','original':'/tmp/nift-status-experiment/nift-baseline'};rows=[]
for size in [100,1000,5000,10000]:
 d=fixtures/f'fixture-{size}-nift';m=json.load(open(d/'manifest.json'));before={a['id']:hashlib.sha256((d/a['output']).read_bytes()).hexdigest() for a in m['actions']}
 for i in range(22):
  order=['experiment1','experiment2'] if i%2==0 else ['experiment2','experiment1']
  if size==10000:order=(['original']+order) if i%2==0 else order+['original']
  for label in order:
   trace=d/'experiment2.trace';trace.unlink(missing_ok=True);old=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.perf_counter_ns();r=subprocess.run([bins[label],'build'],cwd=d,env=os.environ|{'BUILD_TRACE':str(trace)},capture_output=True,text=True);wall=(time.perf_counter_ns()-start)/1e6;new=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.returncode==0 and not trace.exists(),(size,label,r.stdout,r.stderr)
   rows.append({'size':size,'binary':label,'round':i,'warmup':i<2,'wall_ms':wall,'cpu_ms':(new.ru_utime+new.ru_stime-old.ru_utime-old.ru_stime)*1000})
 assert before=={a['id']:hashlib.sha256((d/a['output']).read_bytes()).hexdigest() for a in m['actions']};assert subprocess.check_output([str(d/'build/bin/app.exe')],text=True)==m['expected_stdout'];(p/'paired.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(size,[(label,round(statistics.median(r['wall_ms'] for r in rows if r['size']==size and r['binary']==label and not r['warmup']),3)) for label in order],flush=True)
