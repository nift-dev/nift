#!/usr/bin/env python3
"""Bounded external watchdog for process cleanup tests (also on native Windows)."""
import argparse, json, os, pathlib, subprocess, time
p=argparse.ArgumentParser();p.add_argument('--failure',required=True);p.add_argument('--contract',required=True);p.add_argument('--output');a=p.parse_args()
start=time.monotonic();results=[]
def run(binary,args):
 r=subprocess.run([str(pathlib.Path(binary).resolve()),*args],text=True,capture_output=True,timeout=10)
 results.append(dict(case=args,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
 assert r.returncode==0,results[-1]
run(a.contract,[])
windows=os.name=='nt' or bool(os.environ.get('MSYSTEM'))
for job in ([False] if windows else [False,True]):
 for op,stages in [('temp',range(2)),('pipe',range(2)),('spawn' if windows else 'fork',range(3))]+([('duplicate',range(3))] if windows else []):
  if job and op=='temp':continue
  for stage in stages:run(a.failure,[op,str(stage)]+(['job'] if job else []))
if not windows:
 for job in [False,True]:
  for op in ['dup2-in','dup2-out','dup2-err']:run(a.failure,[op,'0']+(['job'] if job else []))
data=dict(pass_=True,platform=os.name,cases=results,elapsed_seconds=time.monotonic()-start)
if a.output:pathlib.Path(a.output).write_text(json.dumps(data,indent=2)+'\n')
print('process hardening PASS:',len(results),'cases')
