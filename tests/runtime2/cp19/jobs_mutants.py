import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();rows=[]
def run(binary,mode):
 r=subprocess.run([str(a.dir/binary),mode],capture_output=True,text=True,timeout=20);rows.append(dict(binary=binary,mode=mode,status=r.returncode,stdout=r.stdout,stderr=r.stderr));return r
assert run('job-control','normal').returncode==0
for i,mode,witness in [(1,'expect-missing-finish','finish=0 fault=0 count=0'),(2,'expect-missing-fault','finish=1 fault=0 count=1')]:
 r=run('job-m'+str(i),'normal');assert r.returncode==1 and witness in r.stdout
 assert run('job-m'+str(i),mode).returncode==0
 assert run('job-control',mode).returncode==1
 assert run('job-control','unrelated').returncode==2
 dead=subprocess.run(['/bin/false'],capture_output=True,text=True);assert dead.returncode!=0 and witness not in dead.stdout
 rows.append(dict(control='dead',status=dead.returncode,stdout=dead.stdout))
a.out.write_text(json.dumps(rows,indent=2)+'\n');print('PASS CP19 job reporting mutations: 2 mutations, reciprocal/dead/unrelated controls')
