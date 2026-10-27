#!/usr/bin/env python3
"""Retained numeric readers: legacy channels, non-catchability, and exact effects."""
import argparse, hashlib, json, os, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--legacy',required=True);p.add_argument('--nift',required=True)
p.add_argument('--switch',required=True);p.add_argument('--out',type=Path,required=True)
p.add_argument('--expect',choices=['guard','refusal'],default='guard');a=p.parse_args()
cases={
 'shared-capture-reader':'fn(makeo()){a:=[1];return {"putv":v=>{a[0]=v;return a.stringify()},"readv":()=>a[0]*2}};o:=makeo();warm:=o.putv(0);saved:=error("seed")\no.putv("bad");print("MUTATED")\ntry{o.readv()}catch(e){saved=e}\n(saved.message)\nexit\n',
 'unused-reader':'a:=[1];f:=()=>a[0]*2\na[0] = "bad";print("MUTATED")\na.stringify()\nexit\n',
 'lambda-catchability':'a:=[1];f:=()=>a[0]*2;saved:=error("seed")\na[0] = "bad";print("MUTATED")\ntry{f()}catch(e){saved=e}\n(saved.message)\n(saved.code)\nexit\n',
 'rhs-effects':'a:=[1];fn(side()){print("RHS");return 2};f:=()=>a[0]*side()\na[0] = "bad";print("MUTATED")\nprint("BEFORE");f();print("AFTER")\nexit\n',
}
rows=[];a.out.parent.mkdir(parents=True,exist_ok=True)
for case,source in cases.items():
 baseline=None
 for label,binary,mode in [('legacy',a.legacy,'0'),('default-mode1',a.nift,'1'),('default-require',a.nift,'require'),
                           ('switch-mode1',a.switch,'1'),('switch-require',a.switch,'require')]:
  binary=Path(binary).resolve()
  with tempfile.TemporaryDirectory(prefix='cp19-error-boundary-') as td:
   stats=Path(td)/'stats';env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')}
   env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
   r=subprocess.run([str(binary),'-i'],input=source,text=True,capture_output=True,cwd=td,env=env,timeout=30)
   counters=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
   row=dict(case=case,lane=label,source=source,binary=str(binary),sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
            status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=counters,classification='A',expected=a.expect)
   rows.append(row);a.out.write_text(json.dumps(rows,indent=2)+'\n')
   assert r.returncode==0,row
   if label=='legacy':
    baseline=(r.returncode,r.stdout,r.stderr)
    assert r.stdout.count('MUTATED\n')==1,row
    if case=='unused-reader': assert r.stdout=='MUTATED\nnull\n["bad"]\n' and not r.stderr,row
    if case=='lambda-catchability':
     assert 'arithmetic operators require numeric operands' in r.stderr and '"seed"\n' in r.stdout,row
    if case=='rhs-effects': assert 'RHS\n' in r.stdout and 'AFTER' not in r.stdout,row
   elif a.expect=='guard':
    assert (r.returncode,r.stdout,r.stderr)==baseline,row
    assert len(counters)>=3 and all(x['outcome'] in ('vm','vm-error') and x['vm_executions']==1 and x['legacy_executions']==0 for x in counters),row
   else:
    assert len(counters)>=3 and counters[1]['outcome']=='require-error' and counters[1]['reason']=='type:*',row
    assert counters[1]['vm_executions']==0 and all(x['legacy_executions']==0 for x in counters),row
    assert 'MUTATED' not in r.stdout,row
print('PASS retained-reader numeric guard and legacy channels' if a.expect=='guard' else 'VERIFIED former refusal boundary; no feature acceptance claimed')
