import os,re,json,tempfile,subprocess
from pathlib import Path
binary=str(Path(os.environ.get('NIFT_FS_STATS','.build/cp410-implementation/filesystem/stats-nift')).resolve())
def probe(source,expected,outputs=()):
 with tempfile.TemporaryDirectory() as d:
  root=Path(d);(root/'out').mkdir()
  for name in ['a','b','c']:(root/name).write_text(name)
  q=subprocess.run([binary,'-'],input=source,cwd=root,text=True,encoding='utf-8',capture_output=True,env={**os.environ,'NIFT_TEST_FS_RECIPE_STATS':'1'},timeout=60)
  assert q.returncode==0 and q.stdout==expected,(q.returncode,q.stdout,q.stderr)
  for name in outputs:assert (root/'out'/name).read_text()==name,name
  match=re.fullmatch(r'fs-plan (.*)\n',q.stderr);assert match,q.stderr
  stats={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',match[1])}
  return stats
s=probe('paths:=["a","b","c"]; for(p:paths){copy(p,"out/"+p.split("/").last())};print("OK")','OK\n',('a','b','c'))
assert s['prepares']==1 and s['recipes']==3 and s['backends']==3 and s['operands']==6 and s['pure']>=3,s
s=probe('n:=0; fn(src()){n+=1;return "a"};fn(dst()){n+=1;return "out/a"};j:=0;while(j<2){copy(src(),dst());j+=1};print(n)','8\n',('a',))
assert s['prepares']==1 and s['recipes']==2 and s['backends']==2 and s['operands']==4 and s['pure']==0,s
s=probe('paths:=["a","out/a"];j:=0;while(j<1){copy(...paths);j+=1};print("OK")','OK\n',('a',))
assert s['recipes']==0 and s['fallbacks']==1 and s['backends']==1 and s['operands']==2,s
s=probe('exists:=false;j:=0;while(j<3){value:=stat("a");exists=value.exists;j+=1};print(exists)','true\n')
assert s['prepares']==1 and s['recipes']==3 and s['backends']==3 and s['operands']==3,s
print('PASS immutable recipe reuse, canonical operands, pure plans, factories and spread fallback counters')
