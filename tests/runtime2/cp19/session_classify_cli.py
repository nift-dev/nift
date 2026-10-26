#!/usr/bin/env python3
"""Non-executing session classification and whole-line command validation."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
binary=str(Path(a.nift).resolve());records=[];a.out.parent.mkdir(parents=True,exist_ok=True)
for mode in ['classify','classify-all']:
 with tempfile.TemporaryDirectory(prefix='cp19-classify-') as td:
  root=Path(td);stats=root/'stats';(root/'.niftrc').write_text('print("RC-MUST-NOT-RUN")')
  env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
  source='print("SCRIPT-MUST-NOT-RUN")\ncommand /usr/bin/touch marker\ncommand /usr/bin/printf "%s" "$[print(\"INTERPOLATION-MUST-NOT-RUN\")]"\nexit\n'
  r=subprocess.run([binary,'-i'],input=source,cwd=td,env=env,text=True,capture_output=True,timeout=30)
  rows=[json.loads(x) for x in stats.read_text().splitlines()]
  record=dict(case='classification-no-effects',mode=mode,status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=rows,marker_exists=(root/'marker').exists());records.append(record)
  a.out.write_text(json.dumps(records,indent=2)+'\n')
  assert (r.returncode,r.stdout,r.stderr)==(0,'','') and not record['marker_exists'],record
  if mode=='classify-all':
   assert all(x['inventory_complete'] and x['reasons']==[] and x['inventory_stop']=='' for x in rows),record
  assert len(rows)==2 and all(x['outcome']=='compiled' and x['vm_executions']==x['legacy_executions']==0 for x in rows),record
with tempfile.TemporaryDirectory(prefix='cp19-inventory-') as td:
 root=Path(td);stats=root/'stats'
 env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM='classify-all',NIFT_VM_STATS=str(stats),NO_COLOR='1')
 r=subprocess.run([binary,'-i'],input='print("MUST-NOT-RUN");bytes([65]);unknown(1)\nexit\n',cwd=td,env=env,text=True,capture_output=True,timeout=30)
 rows=[json.loads(x) for x in stats.read_text().splitlines()]
 record=dict(case='independent-refusal-inventory',status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=rows);records.append(record)
 a.out.write_text(json.dumps(records,indent=2)+'\n')
 assert (r.returncode,r.stdout,r.stderr)==(0,'','') and len(rows)==1,record
 row=rows[0];assert row['vm_executions']==row['legacy_executions']==0 and row['inventory_complete'],record
 reasons={x['reason'] for x in row['reasons'] if not x['consequential']}
 assert 'builtin-bytes:bytes' in reasons and 'callable:unknown' in reasons,record
for mode in ['1','require']:
 with tempfile.TemporaryDirectory(prefix='cp19-session-budget-') as td:
  root=Path(td);stats=root/'stats'
  env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_HEAP_BUDGET='1',NIFT_VM_STATS=str(stats),NO_COLOR='1')
  r=subprocess.run([binary,'-i'],input='print("MUST-NOT-RUN")\nprint("AFTER-INTERNAL")\n',cwd=td,env=env,text=True,capture_output=True,timeout=30)
  rows=[json.loads(x) for x in stats.read_text().splitlines()]
  record=dict(case='session-budget-terminal',mode=mode,status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=rows);records.append(record)
  a.out.write_text(json.dumps(records,indent=2)+'\n')
  assert r.returncode==70 and r.stdout=='' and 'memory budget exceeded' in r.stderr and len(rows)==1,record
  assert rows[0]['outcome']=='internal-error' and rows[0]['vm_executions']==1 and rows[0]['legacy_executions']==0,record
for mode in ['1','require']:
 with tempfile.TemporaryDirectory(prefix='cp19-interpolation-budget-') as td:
  root=Path(td);stats=root/'stats'
  env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_HEAP_BUDGET='1',NIFT_VM_STATS=str(stats),NO_COLOR='1')
  source='command /usr/bin/touch marker "$[1]" || command /usr/bin/touch fallback\ncommand /usr/bin/touch after\n'
  r=subprocess.run([binary,'-i'],input=source,cwd=td,env=env,text=True,capture_output=True,timeout=30)
  rows=[json.loads(x) for x in stats.read_text().splitlines()]
  record=dict(case='interpolation-budget-terminal',mode=mode,status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=rows,markers=[p.name for p in root.iterdir() if p.name in ('marker','fallback','after')]);records.append(record)
  a.out.write_text(json.dumps(records,indent=2)+'\n')
  assert r.returncode==70 and r.stdout=='' and 'memory budget exceeded' in r.stderr and len(rows)==1 and not record['markers'],record
  assert rows[0]['outcome']=='internal-error' and rows[0]['vm_executions']==1 and rows[0]['legacy_executions']==0,record
for mode in ['1','require']:
 with tempfile.TemporaryDirectory(prefix='cp19-command-budget-') as td:
  root=Path(td);stats=root/'stats'
  env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_HEAP_BUDGET='1',NIFT_VM_STATS=str(stats),NO_COLOR='1')
  source='print hello || command /usr/bin/touch fallback\ncommand /usr/bin/touch after\n'
  r=subprocess.run([binary,'-i'],input=source,cwd=td,env=env,text=True,capture_output=True,timeout=30)
  rows=[json.loads(x) for x in stats.read_text().splitlines()]
  record=dict(case='command-budget-terminal',mode=mode,status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=rows,markers=[p.name for p in root.iterdir() if p.name in ('marker','fallback','after')]);records.append(record)
  a.out.write_text(json.dumps(records,indent=2)+'\n')
  assert r.returncode==70 and r.stdout=='' and 'memory budget exceeded' in r.stderr and len(rows)==1 and not record['markers'],record
  assert rows[0]['outcome']=='internal-error' and rows[0]['vm_executions']==1 and rows[0]['legacy_executions']==0,record
baseline=None
for mode in ['0','1','require']:
 with tempfile.TemporaryDirectory(prefix='cp19-invalid-command-') as td:
  root=Path(td);stats=root/'stats'
  env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
  r=subprocess.run([binary,'-i'],input='command /usr/bin/touch marker |\nexit\n',cwd=td,env=env,text=True,capture_output=True,timeout=30)
  rows=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
  record=dict(case='whole-line-validation',mode=mode,status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=rows,marker_exists=(root/'marker').exists());records.append(record)
  a.out.write_text(json.dumps(records,indent=2)+'\n')
  channels=(r.returncode,r.stdout,r.stderr)
  if baseline is None:baseline=channels
  assert channels==baseline and r.stderr and not record['marker_exists'] and not rows,record
print(f'PASS session classification/command no-effect checks: {len(records)} observations with refusal inventory')
