import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--suffix',default='');a=p.parse_args();rows=[]
for case,i in [('wrapper',1),('field',2),('loop',3),('alias',4),('schema',5),('order',6),('direct',7),('nested',7),('capture',8),('numeric',9),('native',10),('native-once',11),('selector',12),('shared',13)]:
 mutant_expect='internal' if case in ('numeric','native','shared') else 'replayed' if case=='native-once' else 'refused'
 for binary,expect,status in [('admission-control','admitted',0),(f'admission-m{i}',mutant_expect,0),('admission-control',mutant_expect,1),(f'admission-m{i}','admitted',1)]:
  r=subprocess.run([str(a.dir/(binary+a.suffix)),case,expect],text=True,capture_output=True,timeout=30)
  row=dict(binary=binary,case=case,expect=expect,status=r.returncode,stdout=r.stdout,stderr=r.stderr);rows.append(row)
  a.out.write_text(json.dumps(rows,indent=2)+'\n')
  assert r.returncode==status,row
  if status==0: assert f'WITNESS {case} {expect}\n' in r.stdout,row
  else: assert 'CHECK failed:' in r.stderr and 'WITNESS' not in r.stdout,row
print('PASS thirteen compiled admission mutants, exact refusal/state witnesses and bidirectional control discrimination')
