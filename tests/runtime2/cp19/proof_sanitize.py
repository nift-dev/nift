import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();rows=[]
env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'}
for suffix in ['-sanitize','-switch-sanitize']:
 cases=[('reporting-arguments','prefixed'),('history','admitted'),('windows','clean'),('array','clean'),('shared-callable','guarded'),('reporting','prefixed'),('identity','clean'),('lifetime','clean')]
 cases += [(c,'refused') for c in ['unknown-effect','partial-target','active','active-method','active-constructor','active-collection','active-indirect','unknown','field','element','return','alias','cell','unknown-result']]
 for case,expect in cases:
  r=subprocess.run([str(a.dir/('proof'+suffix)),case,expect],env=env,text=True,capture_output=True,timeout=120)
  row=dict(binary='proof'+suffix,case=case,expect=expect,status=r.returncode,stdout=r.stdout,stderr=r.stderr);rows.append(row)
  a.out.write_text(json.dumps(rows,indent=2)+'\n');assert r.returncode==0 and f'WITNESS {case} {expect}' in r.stdout,row
 r=subprocess.run([str(a.dir/('proof-alloc'+suffix))],env=env,text=True,capture_output=True,timeout=180)
 row=dict(binary='proof-alloc'+suffix,status=r.returncode,stdout=r.stdout,stderr=r.stderr);rows.append(row)
 a.out.write_text(json.dumps(rows,indent=2)+'\n');assert r.returncode==0 and 'ALLOCATION-SWEEP' in r.stdout,row
print('PASS summary, current proof, source/lifetime and allocation sweep under ASan/LSan/UBSan, both dispatches')
