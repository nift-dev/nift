import argparse,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();rows=[]
def check(binary,case,expect,status,control):
 with tempfile.TemporaryDirectory(prefix='cp19-resource-mutation-') as td:
  r=subprocess.run([str(binary),td,case,expect],capture_output=True,text=True,timeout=45)
 row=dict(binary=str(binary),case=case,expect=expect,status=r.returncode,stdout=r.stdout,stderr=r.stderr,control=control);rows.append(row);a.out.write_text(json.dumps(rows,indent=2)+'\n')
 assert r.returncode==status,row
 if status==0:assert f'WITNESS {case} {expect}\n' in r.stdout,row
 elif control!='dead':assert 'CHECK failed:' in r.stderr and 'WITNESS' not in r.stdout,row
for suffix in ['', '-switch']:
 for mutant,case in [(1,'survival'),(2,'unwind'),(3,'storage'),(4,'reports'),(5,'capture'),(6,'widening')]:
  normal=a.dir/('resource-control'+suffix);changed=a.dir/(f'resource-m{mutant}'+suffix)
  check(normal,case,'correct',0,'normal');check(changed,case,'broken',0,'named-behavior')
  check(normal,case,'broken',1,'reciprocal');check(changed,case,'correct',1,'reciprocal')
  check(Path('/usr/bin/false'),case,'broken',1,'dead');check(normal,'storage','broken',1,'unrelated')
print('PASS six production resource mutations; both dispatches and exact reciprocal/dead/unrelated controls')
