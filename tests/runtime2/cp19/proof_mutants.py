import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--suffix',default='');a=p.parse_args();rows=[]
def check(binary,case,expect,status,kind='witness'):
 r=subprocess.run([str(binary),case,expect],text=True,capture_output=True,timeout=60)
 row=dict(binary=str(binary),case=case,expect=expect,status=r.returncode,stdout=r.stdout,stderr=r.stderr,control=kind);rows.append(row)
 a.out.write_text(json.dumps(rows,indent=2)+'\n')
 assert r.returncode==status,row
 if status==0: assert f'WITNESS {case} {expect}\n' in r.stdout,row
 elif kind!='dead': assert 'CHECK failed:' in r.stderr and 'WITNESS' not in r.stdout,row
cases=[(25,"unknown-effect","refused","admitted"),(26,"partial-target","refused","admitted"),(24,'reporting-arguments','prefixed','lost'),(23,'windows','clean','broken'),(13,'shared-callable','guarded','internal'),(22,'array','clean','broken'),(14,'history','admitted','refused'),(15,'active','refused','internal'),
 (18,'unknown-result','refused','admitted'),(19,'identity','clean','wrong-source'),
 (20,'lifetime','clean','cached'),(21,'reporting','prefixed','raw')]
for case in ['active-method','active-constructor','active-collection','active-indirect']:
 cases.append((15,case,'refused','internal'))
for case in ['unknown','field','element','return','alias','cell']:
 cases.extend([(16,case,'refused','untainted'),(17,case,'refused','internal')])
for mutant,case,good,bad in cases:
 control=a.dir/('proof'+a.suffix);changed=a.dir/(f'proof-m{mutant}'+a.suffix)
 check(control,case,good,0);check(changed,case,bad,0)
 check(control,case,bad,1);check(changed,case,good,1)
 # A dead binary and a working but unrelated assertion failure cannot satisfy the positive witness.
 check(Path('/usr/bin/false'),case,bad,1,'dead')
 check(control,'identity','wrong-source',1,'unrelated-failure')
print('PASS fourteen compiled summary/reporting mutants; exact behaviors and dead/unrelated controls')
