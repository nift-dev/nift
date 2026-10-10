import pathlib,sys,json,os,subprocess,time,statistics,shutil
p=pathlib.Path('/tmp/nift-noop-investigation');sys.path.insert(0,str(p/'suite/scripts'));from certify import mutate,dependency_audit
os.sched_setaffinity(0,set(json.loads((p/'identity.json').read_text())['cpus']));rows=json.loads((p/'changed.json').read_text())
for size in [10000]:
 base=p/f'fixture-{size}-nift';m=json.loads((base/'manifest.json').read_text())
 for scenario in ['leaf','private','medium','global']:
  samples=[];count=1 if scenario=='global' and size>=5000 else 3
  for i in range(count):
   d=p/'changed-working';shutil.rmtree(d,ignore_errors=True);shutil.copytree(base,d);expected,delta,target=mutate(d,m,scenario);trace=d/'changed.trace';env=os.environ.copy();env.update(BUILD_TRACE=str(trace),LC_ALL='C',TZ='UTC');t=time.perf_counter();r=subprocess.run(['/home/nick/Repositories/nift/nift/nift','build'],cwd=d,env=env,capture_output=True,text=True);elapsed=(time.perf_counter()-t)*1000;executed=trace.read_text().splitlines() if trace.exists() else [];assert r.returncode==0,(size,scenario,r.stderr);assert sorted(executed)==expected and len(executed)==len(set(executed)),(size,scenario,len(executed),len(expected));assert subprocess.check_output([str(d/'build/bin/app.exe')],text=True)==str(int(m['expected_stdout'])+delta)+'\n';audit=dependency_audit(d,'nift',m);samples.append({'wall_ms':elapsed,'action_count':len(executed),'action_oracle':True,'stdout_oracle':True,'include_audit':audit})
  row={'size':size,'scenario':scenario,'samples':samples,'median_ms':statistics.median(x['wall_ms'] for x in samples),'sample_count':count};rows.append(row);(p/'changed.json').write_text(json.dumps(rows,indent=2)+'\n');print(size,scenario,round(row['median_ms'],3),len(expected),flush=True)
shutil.rmtree(p/'changed-working')
