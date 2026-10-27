#!/usr/bin/env python3
"""Recheck the existing ordinary 1M-catch workloads; no new performance waiver."""
import argparse,hashlib,json,os,platform,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--switch',required=True)
p.add_argument('--reference',type=Path,required=True);p.add_argument('--reference-switch',type=Path,required=True)
p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True)
refs={'default':json.loads(a.reference.read_text()),'switch':json.loads(a.reference_switch.read_text())};rows=[]
meta=dict(valgrind=subprocess.check_output(['valgrind','--version'],text=True).strip(),platform=platform.platform(),
          qualification='Required ordinary-runtime regression check only; historical matched-dispatch references; no threshold or waiver changed')
(a.out/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
for dispatch,binary in [('default',a.nift),('switch',a.switch)]:
 binary=Path(binary).resolve()
 for workload in ['caught','caught_cell']:
  source=Path(f'tests/runtime2/cp13b/stress/stress_{workload}.f.in').read_text().replace('@COUNT@','1000000')
  folder=a.out/(dispatch+'-'+workload);folder.mkdir();(folder/'p.f').write_text(source)
  profile=folder/'callgrind.out';stats=folder/'stats.jsonl'
  env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')}
  env.update(HOME=str(folder),NIFT_VM='require',NIFT_PREVALIDATE='off',NIFT_VM_STATS=str(stats),NIFT_VM_HEAP_BUDGET='8000000')
  command=['valgrind','--tool=callgrind','--callgrind-out-file='+str(profile),str(binary),'p.f']
  r=subprocess.run(command,cwd=folder,env=env,text=True,capture_output=True,timeout=900)
  (folder/'stdout.txt').write_text(r.stdout);(folder/'stderr.txt').write_text(r.stderr)
  match=re.search(r'^summary: (\d+)',profile.read_text(),re.M) if profile.exists() else None
  counters=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
  digest=hashlib.sha256(source.encode()).hexdigest()
  base=next(x for x in refs[dispatch] if x['binary']=='base' and x['workload']==workload)
  assert base['source_sha256']==digest,'Reference source mismatch'
  count=int(match[1]) if match else None
  row=dict(dispatch=dispatch,workload=workload,binary=str(binary),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
           argv=command,source_sha256=digest,status=r.returncode,instructions=count,stats=counters,
           reference_binary_sha256=base['sha256'],reference_instructions=base['instructions'],
           ratio=count/base['instructions'] if count else None,threshold_changed=False,waiver=False)
  rows.append(row);(a.out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
  assert r.returncode==0 and r.stdout=='ok\n' and match and len(counters)==1,row
  assert counters[0]['outcome']=='vm' and counters[0]['vm_executions']==1 and counters[0]['legacy_executions']==0,row
  print(dispatch,workload,count,f"{(row['ratio']-1)*100:+.6f}%",flush=True)
print('Recorded four matched-dispatch observations; no instruction gate or performance obligation declared passed')
