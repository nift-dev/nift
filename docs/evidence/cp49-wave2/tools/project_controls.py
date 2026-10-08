"""Real CLI project controls: load/status/fullbuild/incremental dependency rebuild."""
import pathlib,subprocess,json,hashlib,shutil,time,resource,statistics
b=pathlib.Path('.build/cp49-wave2/projects');b.mkdir(parents=True,exist_ok=True)
bins=[pathlib.Path(p).resolve() for p in ['.build/cp49-campaign/start-nift','.build/cp49-wave2/start-nift','.build/cp49-wave2/final-nift']];results=[];expected=None
for i,binary in enumerate(bins):
 root=b/str(i)
 if root.exists():shutil.rmtree(root)
 root.mkdir()
 def run(args):
  q=subprocess.run([str(binary),*args],cwd=root,capture_output=True,text=True);assert q.returncode==0,(args,q.stdout,q.stderr);return q
 run(['init'])
 for page in range(20):
  name='p'+str(page);run(['track',name]);p=root/'content'/(name+'.html');p.write_text('@fn(step(x)) { return x + 1 }\n$[s := 0]\n@for(i : range(0,1000)) { $[s += step(i)] }\nSUM=$[s]\n')
 run(['build','--all'])
 assert 'SUM=500500' in (root/'public/p0.html').read_text()
 def hashes():return {str(p.relative_to(root/'public')):hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'public').rglob('*') if p.is_file()}
 output=hashes();expected=output if expected is None else expected;assert output==expected
 samples=[]
 for label,args in [('load',['info']),('status',['status']),('fullbuild',['build','--all']),('incremental',['build'])]:
  for rep in range(5):
   before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();q=run(args);wall=time.perf_counter()-t;after=resource.getrusage(resource.RUSAGE_CHILDREN)
   samples.append({'phase':label,'cpu_ms':1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime),'wall_ms':1000*wall})
  assert hashes()==expected
 # Editing the common template must rebuild dependent pages and equal fullbuild.
 template=root/'templates/template.html';assert template.exists();template.write_text(template.read_text()+'\nDEPENDENCY-CONTROL\n');run(['build']);incremental=hashes();run(['build','--all']);assert incremental==hashes();assert incremental!=expected
 results.append({'binary':str(binary),'outputs':len(expected),'output_sha256':expected,'samples':samples,'dependency_matches_fullbuild':True})
 print(i,'project controls PASS',len(expected),'files',flush=True)
(b/'results.json').write_text(json.dumps(results,indent=2)+'\n')
