#!/usr/bin/env python3
"""Exact whole-CLI 1M-catch instruction counts, accepted base and both dispatch modes."""
import argparse,hashlib,json,os,re,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--base',required=True);p.add_argument('--nift',required=True);p.add_argument('--switch',required=True);p.add_argument('--out',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[3];out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
results=[]
for name,binary in [('base',Path(a.base).resolve()),('default',Path(a.nift).resolve()),('switch',Path(a.switch).resolve())]:
 for workload in ['caught','caught_cell']:
  source=(root/f'tests/runtime2/cp13b/stress/stress_{workload}.f.in').read_text().replace('@COUNT@','1000000')
  folder=out/(name+'-'+workload);folder.mkdir(exist_ok=True)
  work=out/('work-'+workload);work.mkdir(exist_ok=True);(work/'p.f').write_text(source)
  stats=folder/'stats.jsonl';stats.unlink(missing_ok=True);profile=folder/'callgrind.out'
  env=dict(os.environ,NIFT_VM='require',NIFT_PREVALIDATE='off',NIFT_VM_STATS=str(stats),NIFT_VM_HEAP_BUDGET='8000000')
  proc=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(profile),str(binary),'p.f'],cwd=work,env=env,capture_output=True,text=True,timeout=900)
  (folder/'stdout.txt').write_text(proc.stdout);(folder/'stderr.txt').write_text(proc.stderr)
  match=re.search(r'^summary: (\d+)',profile.read_text(),re.M) if profile.exists() else None
  st=[json.loads(s) for s in stats.read_text().splitlines()] if stats.exists() else []
  ok=proc.returncode==0 and proc.stdout=='ok\n' and bool(match) and st and st[-1]['outcome']=='vm' and st[-1]['legacy_executions']==0
  results.append(dict(binary=name,workload=workload,source_sha256=hashlib.sha256(source.encode()).hexdigest(),source_path=str(work/'p.f'),sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),instructions=int(match[1]) if match else None,exit=proc.returncode,pass_=bool(ok),stats=st))
  (out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(name,workload,results[-1]['instructions'],ok,flush=True)
comparisons=[]
for workload in ['caught','caught_cell']:
 base=next(r['instructions'] for r in results if r['binary']=='base' and r['workload']==workload)
 for r in results:
  if r['binary']!='base' and r['workload']==workload:comparisons.append(dict(binary=r['binary'],workload=workload,ratio=r['instructions']/base,delta=r['instructions']-base))
(out/'comparisons.json').write_text(json.dumps(comparisons,indent=2)+'\n')
raise SystemExit(0 if all(r['pass_'] for r in results) else 1)
