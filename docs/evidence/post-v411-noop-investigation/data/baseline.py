import pathlib,shutil,subprocess,os,time,json,statistics,hashlib,resource
p=pathlib.Path('/tmp/nift-noop-investigation');exp=pathlib.Path('/home/nick/Repositories/nift/nift-experiments/build-systems');nift='/home/nick/Repositories/nift/nift/nift';ninja='/tmp/build-systems-tools/ninja/usr/bin/ninja';cpus=sorted(os.sched_getaffinity(0))[:4];os.sched_setaffinity(0,set(cpus));rows=[]
locations={100:'certify-native-100-light-j4',1000:'certify-native-1000-light-j2',5000:'certify-5000-light-j4',10000:'certify-10000-light-j4'}
identity={'cpus':cpus,'core_SHA':subprocess.check_output(['git','rev-parse','HEAD'],cwd=pathlib.Path(nift).parent,text=True).strip(),'binary_sha256':hashlib.sha256(pathlib.Path(nift).read_bytes()).hexdigest(),'tools':{},'lscpu':subprocess.check_output(['lscpu'],text=True),'filesystem':subprocess.check_output(['df','-T',str(p)],text=True),'uname':subprocess.check_output(['uname','-a'],text=True)}
for name,cmd in [('make',['make','--version']),('ninja',[ninja,'--version']),('compiler',['g++','--version']),('linker',['ld','--version'])]:identity['tools'][name]=subprocess.check_output(cmd,text=True).splitlines()[0]
(p/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
for size,loc in locations.items():
 for system in ['make','ninja','nift']:
  dest=p/f'fixture-{size}-{system}';shutil.copytree(exp/'work'/loc/(system+'-baseline'),dest)
  if system=='nift':
   c=dest/'.nift/config.json';d=json.loads(c.read_text());d['config']['build-threads']=4;c.write_text(json.dumps(d,indent=2)+'\n')
  command=[nift,'build'] if system=='nift' else ['make' if system=='make' else ninja,'-j4']
  samples=[];env=os.environ.copy();env.update(BUILD_TRACE=str(dest/'diagnostic.trace'),LC_ALL='C',TZ='UTC')
  for i in range(12):
   trace=dest/'diagnostic.trace';trace.unlink(missing_ok=True);before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();r=subprocess.run(command,cwd=dest,env=env,capture_output=True,text=True);wall=time.perf_counter()-t;after=resource.getrusage(resource.RUSAGE_CHILDREN)
   assert r.returncode==0,(size,system,r.stderr);assert not trace.exists() or trace.read_text()=='',(size,system,'unexpected action')
   samples.append({'warmup':i<2,'wall_ms':wall*1000,'cpu_ms':(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime)*1000})
  row={'size':size,'system':system,'command':command,'median_ms':statistics.median(s['wall_ms'] for s in samples[2:]),'min_ms':min(s['wall_ms'] for s in samples[2:]),'max_ms':max(s['wall_ms'] for s in samples[2:]),'zero_actions':True,'samples':samples};rows.append(row);(p/'baseline.json').write_text(json.dumps(rows,indent=2)+'\n');print(size,system,round(row['median_ms'],3),flush=True)
