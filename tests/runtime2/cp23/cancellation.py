#!/usr/bin/env python3
"""Completion races must have one stable catchable outcome on every await/join."""
import argparse, pathlib, subprocess, os, json, hashlib, tempfile
p=argparse.ArgumentParser();p.add_argument('--nift',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();binary=a.nift.resolve()
report={'binary':str(binary),'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'runs':[],'passed':False}
def save():a.output.write_text(json.dumps(report,indent=2)+'\n')
with tempfile.TemporaryDirectory(prefix='cp23-cancellation-') as temp:
 path=pathlib.Path(temp)/'case.f'
 for workers in (1,2,4,8):
  for thread in (False,True):
   for delay in (0,1,4,12,25):
    for repeat in range(3):
     declaration='fn(f()){sleep(12);return 7}' if thread else 'fn[async](f()){sleep(12);return 7}'
     start='thread(f)' if thread else 'f()';awaited='p.join()' if thread else 'await p'
     source=declaration+f';p:={start};sleep({delay});cancel(p);cancel(p);'+f'try{{print({awaited})}}catch(e){{print(e.code)}};try{{print({awaited})}}catch(e){{print(e.code)}};print(p.done())'
     path.write_text(source);stats=pathlib.Path(temp)/'stats.jsonl';stats.unlink(missing_ok=True)
     proc=subprocess.run([str(binary),str(path)],env=dict(os.environ,NIFT_VM='require',NIFT_VM_PARALLEL_WORKERS=str(workers),NIFT_VM_STATS=str(stats)),capture_output=True,text=True,timeout=10)
     st=json.loads(stats.read_text().splitlines()[-1]) if stats.exists() else {}
     row={'workers':workers,'thread':thread,'delay':delay,'repeat':repeat,'source':source,'exit':proc.returncode,'stdout':proc.stdout,'stderr':proc.stderr,'stats':st}
     row['passed']=proc.returncode==0 and proc.stdout in ('7\n7\ntrue\n','runtime.cancelled\nruntime.cancelled\ntrue\n') and st.get('vm_executions')==1 and st.get('legacy_executions')==0
     report['runs'].append(row);save()
     if not row['passed']:raise SystemExit('cancellation race failed: '+str(row))
 for workers in (1,2,4,8):
  for label,source,expected in [
   ('script-error-after-cancel','fn[async](f()){sleep(30);throw error("original","user.original")};p:=f();sleep(5);cancel(p);try{await p}catch(e){print(e.code);print(e.message)};try{await p}catch(e){print(e.code);print(e.message)}','user.original\noriginal\nuser.original\noriginal\n'),
   ('consumed-cancellation','ready:=channel(0);gate:=channel(0);fn[async](f()){send(ready,1);try{recv(gate)}catch(e){print(e.code)};return 9};p:=f();recv(ready);sleep(2);cancel(p);print(await p);print(await p)','runtime.cancelled\n9\n9\n')]:
   path.write_text(source);proc=subprocess.run([str(binary),str(path)],env=dict(os.environ,NIFT_VM='require',NIFT_VM_PARALLEL_WORKERS=str(workers)),capture_output=True,text=True,timeout=10)
   row={'label':label,'workers':workers,'source':source,'exit':proc.returncode,'stdout':proc.stdout,'stderr':proc.stderr,'passed':proc.returncode==0 and proc.stdout==expected};report['runs'].append(row);save()
   if not row['passed']:raise SystemExit('cancellation semantics failed: '+str(row))
report['passed']=True;save();print('PASS 120 completion races and 8 Error/consumed-cancellation cases; repeated observations; 1/2/4/8 permits')
