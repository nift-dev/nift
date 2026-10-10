import pathlib,sys,json,os,subprocess,time,statistics,shutil,hashlib,resource
p=pathlib.Path('/tmp/nift-status-experiment');f=pathlib.Path('/tmp/nift-noop-investigation');sys.path.insert(0,str(f/'suite/scripts'));from certify import mutate,dependency_audit
os.sched_setaffinity(0,{0,1,2,3});rows=[];bins={'baseline':str(p/'nift-baseline'),'candidate':'/home/nick/Repositories/nift/nift/nift'}
for n in [100,1000,5000,10000]:
 base=f/f'fixture-{n}-nift';m=json.load(open(base/'manifest.json'))
 for scenario in ['leaf','private','medium','global']:
  count=1 if scenario=='global' and n>=5000 else 3
  for i in range(count):
   maps=[]
   for label in (['baseline','candidate'] if i%2==0 else ['candidate','baseline']):
    d=p/'changed-working';shutil.rmtree(d,ignore_errors=True);shutil.copytree(base,d);expected,delta,target=mutate(d,m,scenario);trace=d/'changed.trace';env=os.environ|{'BUILD_TRACE':str(trace),'LC_ALL':'C','TZ':'UTC'};old=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter_ns();r=subprocess.run([bins[label],'build'],cwd=d,env=env,capture_output=True,text=True);wall=(time.perf_counter_ns()-t)/1e6;new=resource.getrusage(resource.RUSAGE_CHILDREN);executed=trace.read_text().splitlines() if trace.exists() else [];assert r.returncode==0,(n,scenario,label,r.stderr);assert sorted(executed)==expected and len(executed)==len(set(executed)),(n,scenario,label,len(executed),len(expected));assert subprocess.check_output([str(d/'build/bin/app.exe')],text=True)==str(int(m['expected_stdout'])+delta)+'\n';audit=dependency_audit(d,'nift',m);mapping={a['id']:hashlib.sha256((d/a['output']).read_bytes()).hexdigest() for a in m['actions']};maps.append(mapping);row={'size':n,'scenario':scenario,'round':i,'binary':label,'wall_ms':wall,'cpu_ms':(new.ru_utime+new.ru_stime-old.ru_utime-old.ru_stime)*1000,'action_count':len(executed),'action_oracle':True,'stdout_oracle':True,'include_audit':audit,'output_map_sha256':hashlib.sha256(json.dumps(mapping,sort_keys=True).encode()).hexdigest()};rows.append(row);(p/'changed-paired.json').write_text(json.dumps(rows,indent=2)+'\n')
   assert maps[0]==maps[1],(n,scenario,'binary output mismatch')
  print(n,scenario,[(name,round(statistics.median(r['wall_ms'] for r in rows if r['size']==n and r['scenario']==scenario and r['binary']==name),2)) for name in bins],flush=True)
shutil.rmtree(p/'changed-working')
