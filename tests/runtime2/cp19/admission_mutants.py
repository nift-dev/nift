import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();rows=[]
for i,case in enumerate(['wrapper','field','loop','alias'],1):
 for binary,expect,status in [('admission-control','admitted',0),(f'admission-m{i}','refused',0),('admission-control','refused',1),(f'admission-m{i}','admitted',1)]:
  r=subprocess.run([str(a.dir/binary),case,expect],text=True,capture_output=True,timeout=30)
  row=dict(binary=binary,case=case,expect=expect,status=r.returncode,stdout=r.stdout,stderr=r.stderr);rows.append(row)
  a.out.write_text(json.dumps(rows,indent=2)+'\n')
  assert r.returncode==status,row
  if status==0: assert f'WITNESS {case} {expect}\n' in r.stdout,row
  else: assert 'CHECK failed:' in r.stderr and 'WITNESS' not in r.stdout,row
print('PASS four compiled admission mutants, exact refusal/state witnesses and bidirectional control discrimination')
