#!/usr/bin/env python3
"""Uncaught join reporting, with caught-Error semantics checked against R2."""
import argparse, hashlib, json, os, pathlib, subprocess, tempfile
p=argparse.ArgumentParser()
p.add_argument('--nift',type=pathlib.Path,required=True)
p.add_argument('--switch',type=pathlib.Path,required=True)
p.add_argument('--r2',type=pathlib.Path,required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();bins={k:getattr(a,k).resolve() for k in ('nift','switch','r2')}
setup='m:=mutex(0);m.lock();fn(bad(m)){m.unlock();return 1};'
cases={
 'direct':setup+'t:=thread(bad,m);t.join()',
 'nested':setup+'fn(outer(m)){t:=thread(bad,m);return t.join()};t:=thread(outer,m);t.join()',
 'caught':setup+'t:=thread(bad,m);try{t.join()}catch(e){print(e.message);print(e.code)};m.unlock()',
 'identity':setup+'t:=thread(bad,m);prior_error:=error("sentinel");try{t.join()}catch(e){prior_error=e};try{t.join()}catch(e){print(e==prior_error);print(e.message)};m.unlock()',
 'rethrow':setup+'t:=thread(bad,m);try{t.join()}catch(e){throw e}',
 'internal-catch':'m:=mutex(0);m.lock();fn(bad(m)){try{m.unlock()}catch(e){return e.message}};t:=thread(bad,m);print(t.join());m.unlock()',
 'ordinary':'m:=mutex(0);m.unlock()',
 'division':'fn(bad()){return 1/0};t:=thread(bad);t.join()',
}
report={'binaries':{k:{'path':str(v),'sha256':hashlib.sha256(v.read_bytes()).hexdigest()} for k,v in bins.items()},'rows':[],'passed':False}
def save():a.output.write_text(json.dumps(report,indent=2)+'\n')
with tempfile.TemporaryDirectory(prefix='cp23-thread-diagnostic-') as temp:
 root=pathlib.Path(temp)
 for name,source in cases.items():
  path=root/(name+'.f');path.write_text(source);rows={}
  for kind,binary in bins.items():
   stats=root/'stats';stats.unlink(missing_ok=True)
   q=subprocess.run([str(binary),str(path)],env=dict(os.environ,NIFT_VM='require',NIFT_VM_STATS=str(stats),NIFT_VM_PARALLEL_WORKERS='2'),capture_output=True,text=True,timeout=15)
   st=json.loads(stats.read_text().splitlines()[-1]) if stats.exists() else {}
   row=dict(case=name,binary=kind,source=source,exit=q.returncode,stdout=q.stdout,stderr=q.stderr,stats=st)
   report['rows'].append(row);rows[kind]=row;save()
   assert st.get('vm_executions')==1 and st.get('legacy_executions')==0,row
  old=rows['r2']
  for kind in ('nift','switch'):
   row=rows[kind]
   assert row['exit']==old['exit'] and row['stdout']==old['stdout'],row
   if name in ('direct','nested'):
    prefix='thread: '*(1 if name=='direct' else 2)
    assert prefix not in old['stderr'],old
    assert row['stderr']==old['stderr'].replace('mutex: unlock by non-owner',prefix+'mutex: unlock by non-owner'),row
   else:
    assert row['stderr']==old['stderr'],row
  if name=='identity':assert rows['nift']['stdout']=='true\nmutex: unlock by non-owner\n'
report['passed']=True;save();print('PASS 24 VM executions: uncaught/nested prefixes; caught identity, internal catch, rethrow, ordinary and division diagnostics unchanged')
