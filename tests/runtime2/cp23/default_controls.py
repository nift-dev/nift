#!/usr/bin/env python3
"""Exact CP23 default diagnostic contract; real VM stats remain mandatory."""
import argparse,hashlib,json,os,pathlib,subprocess,sys,tempfile,shutil
p=argparse.ArgumentParser();p.add_argument('--nift',required=True,type=pathlib.Path);p.add_argument('--r2',required=True,type=pathlib.Path);p.add_argument('--output',required=True,type=pathlib.Path);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[3];binary=a.nift.resolve();old=a.r2.resolve();rows=[]
corpus_tmp=tempfile.TemporaryDirectory(prefix='cp23-default-corpus-');corpus=pathlib.Path(corpus_tmp.name)
for f in (root/'tests/runtime2/cp22/cases').glob('th_error_uncaught.*'):shutil.copy(f,corpus/f.name)
def gate(name,exe,passed,env=None):
 q=subprocess.run([sys.executable,str(root/'tests/runtime2/cp22/run.py'),'--nift',str(exe),'--mode','default','--cases',str(corpus)],capture_output=True,text=True,env=dict(os.environ,**(env or {})),timeout=20)
 good=(q.returncode==0 and 'PASS cp22 default: 1 case(s), 0 failure(s)' in q.stdout) if passed else (q.returncode!=0 and 'FAIL default th_error_uncaught.f:' in q.stdout)
 rows.append(dict(name=name,exit=q.returncode,stdout=q.stdout,stderr=q.stderr,accepted=passed,control_passed=good));assert good,rows[-1]
gate('normal',binary,True);gate('actual-R2-missing-prefix',old,False)
with tempfile.TemporaryDirectory(prefix='cp23-default-controls-') as d:
 for name,change in [('duplicate','err=err.replace(b"thread: boom",b"thread: thread: boom")'),('wrong-location','err=err.replace(b":1:13:",b":1:14:")'),('prefix-on-stdout','out=out.replace(b"start\\n",b"thread: start\\n")')]:
  wrapper=pathlib.Path(d)/name;wrapper.write_text('#!'+sys.executable+'\nimport os,subprocess,sys\nq=subprocess.run('+repr([str(binary)])+'+sys.argv[1:],capture_output=True)\nout,err=q.stdout,q.stderr\n'+change+'\nsys.stdout.buffer.write(out);sys.stderr.buffer.write(err);sys.exit(q.returncode)\n');wrapper.chmod(0o755);gate(name,wrapper,False)
 dead=pathlib.Path(d)/'dead';dead.write_text('#!/bin/sh\nexit 0\n');dead.chmod(0o755);gate('dead',dead,False)
 timeout=pathlib.Path(d)/'timeout';timeout.write_text('#!'+sys.executable+'\nimport time\ntime.sleep(3)\n');timeout.chmod(0o755);gate('actual-timeout',timeout,False,{'CP22_TIMEOUT':'1'})
a.output.write_text(json.dumps({'candidate_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'r2_sha256':hashlib.sha256(old.read_bytes()).hexdigest(),'rows':rows,'passed':True},indent=2)+'\n');print('PASS exact default contract: positive VM run; missing/duplicate/wrong-location/misapplied prefixes, dead binary and actual timeout rejected')
