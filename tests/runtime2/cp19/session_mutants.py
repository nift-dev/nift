import argparse,json,subprocess
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--dir',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--suffix',default='');a=ap.parse_args()
rows=[]
for binary,label in [('session-control','control'),('session-m1','publish'),('session-m2','state'),('session-m3','source'),('session-m4','runtime'),('session-m5','owner'),('session-m6','assignment'),('session-m7','contract')]:
 p=subprocess.run([str(a.dir/(binary+a.suffix)),label],capture_output=True,text=True,timeout=30)
 row=dict(binary=binary,label=label,status=p.returncode,stdout=p.stdout,stderr=p.stderr);rows.append(row)
 assert p.returncode==0 and f'WITNESS {label}\n' in p.stdout,row
 if label!='control':
  p=subprocess.run([str(a.dir/('session-control'+a.suffix)),label],capture_output=True,text=True,timeout=30)
  row=dict(binary='session-control',wrong_expectation=label,status=p.returncode,stdout=p.stdout,stderr=p.stderr);rows.append(row)
  assert p.returncode==1 and 'CHECK failed:' in p.stderr and 'WITNESS' not in p.stdout,row
 a.out.write_text(json.dumps(rows,indent=2)+'\n')
for binary,status in [('session-control',0),('session-m7',1)]:
 p=subprocess.run([str(a.dir/(binary+a.suffix)),'widened'],capture_output=True,text=True,timeout=30)
 row=dict(binary=binary,label='widened',status=p.returncode,stdout=p.stdout,stderr=p.stderr);rows.append(row)
 assert p.returncode==status,row
 if status==0: assert 'WITNESS widened\n' in p.stdout,row
 else: assert 'CHECK failed:' in p.stderr and 'WITNESS' not in p.stdout,row
 a.out.write_text(json.dumps(rows,indent=2)+'\n')
print('PASS 7 compiled session mutants with exact behavioral witnesses and control discrimination')
