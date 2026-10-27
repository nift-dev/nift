#!/usr/bin/env python3
"""Record the unresolved dormant higher-order import gap without accepting it."""
import argparse, hashlib, json, os, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--legacy',required=True);p.add_argument('--nift',required=True)
p.add_argument('--switch',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
source='a:=[1];fn(g(p)){return p(0)+a[0]};warm:=g(v=>v+1)\na.push("bad");print("FORBIDDEN")\na.stringify()\ng(v=>v+1)\nexit\n'
rows=[];a.out.parent.mkdir(parents=True,exist_ok=True)
for lane,binary,mode in [('legacy',a.legacy,'0'),('default-mode1',a.nift,'1'),('default-require',a.nift,'require'),
                         ('switch-mode1',a.switch,'1'),('switch-require',a.switch,'require')]:
 binary=Path(binary).resolve()
 with tempfile.TemporaryDirectory(prefix='cp19-higher-order-') as td:
  stats=Path(td)/'stats';env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')}
  env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
  r=subprocess.run([str(binary),'-i'],input=source,text=True,capture_output=True,cwd=td,env=env,timeout=30)
  counters=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
  row=dict(lane=lane,binary=str(binary),sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),source=source,
           status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=counters,classification='A-unresolved')
  rows.append(row);a.out.write_text(json.dumps(rows,indent=2)+'\n')
  assert r.returncode==0,row
  if lane=='legacy': assert not r.stderr and r.stdout=='FORBIDDEN\nnull\n[1,"bad"]\n2\n',row
  else:
   assert r.stdout=='[1]\n2\n' and 'FORBIDDEN' not in r.stdout,row
   assert len(counters)==4 and counters[1]['outcome']=='require-error',row
   assert counters[1]['reason']=='session:callable-flow' and counters[1]['vm_executions']==0,row
   assert all(x['legacy_executions']==0 for x in counters),row
print('VERIFIED unresolved A gap: historical higher-order parameter facts; refusal preserves state/effects')
