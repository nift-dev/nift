#!/usr/bin/env python3
"""VM-only sleep admission matrix and actual frozen pre-fix negative control.
Local latency is exploratory; this does not certify the 0.2 s reference gate."""
import argparse,hashlib,json,os,pathlib,platform,subprocess,tempfile,time
p=argparse.ArgumentParser();p.add_argument('--nift',type=pathlib.Path,required=True);p.add_argument('--switch',type=pathlib.Path,required=True);p.add_argument('--negative-control',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
bins={k:getattr(a,k).resolve() for k in ('nift','switch','negative_control')}
report={'platform':platform.platform(),'affinity':sorted(os.sched_getaffinity(0)),'cpuinfo':pathlib.Path('/proc/cpuinfo').read_text(),'binaries':{k:{'path':str(v),'sha256':hashlib.sha256(v.read_bytes()).hexdigest()} for k,v in bins.items()},'rows':[],'reference_latency_gate_seconds':0.2,'reference_latency_certified':False,'passed':False}
def save():a.output.write_text(json.dumps(report,indent=2)+'\n')
source='fn[async](f(i)){sleep(10);return i};hs:=[];i:=0;while(i<N){hs.push(f(i));i+=1};s:=0;for(h:hs){s+=await h};print(s)'
with tempfile.TemporaryDirectory(prefix='cp23-admission-') as temp:
 root=pathlib.Path(temp);path=root/'sleep.f';stats=root/'stats.jsonl'
 def run(kind,n,permits):
  text=source.replace('i<N','i<'+str(n));path.write_text(text);stats.unlink(missing_ok=True)
  env=dict(os.environ,NIFT_VM='require',NIFT_VM_PARALLEL_WORKERS=str(permits),NIFT_VM_STATS=str(stats))
  start=time.perf_counter();q=subprocess.run([str(bins[kind]),str(path)],env=env,capture_output=True,text=True,timeout=120)
  st=json.loads(stats.read_text().splitlines()[-1]) if stats.exists() else {}
  return {'kind':kind,'tasks':n,'cpu_permits':permits,'source':text,'source_sha256':hashlib.sha256(text.encode()).hexdigest(),'wall_seconds':time.perf_counter()-start,'exit':q.returncode,'stdout':q.stdout,'stderr':q.stderr,'stats':st}
 old=run('negative_control',200,2);old['negative_control_detected']=old['exit']==1 and 'parallel task admission limit exceeded' in old['stderr'];report['before']=old;save()
 if not old['negative_control_detected']:raise SystemExit('actual pre-fix control not detected')
 for kind in ('nift','switch'):
  for permits in (1,2,4,8):
   for n in (200,10000):
    row=run(kind,n,permits);row['passed']=row['exit']==0 and row['stdout']==str(n*(n-1)//2)+'\n' and row['stats'].get('vm_executions')==1 and row['stats'].get('legacy_executions')==0
    report['rows'].append(row);save()
    if not row['passed']:raise SystemExit('admission regression: '+str(row))
report['passed']=True;save();print('PASS 16 VM-only sleep cases; 200/10000 tasks; 1/2/4/8 CPU permits; actual R2 negative control detected')
