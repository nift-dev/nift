#!/usr/bin/env python3
"""Fresh-process exhaustive bounded allocation sites, one-shot or persistent.
The binary verifies internal-stop semantics, owned-heap balance and registry
release. Any signal/timeout/setup failure fails the campaign."""
import argparse,hashlib,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--binary',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--persistent',action='store_true');p.add_argument('--mode',choices=['awaited','unawaited','cycle','cancelled','timers'],default='awaited');args=p.parse_args();args.binary=args.binary.resolve()
report={'binary':str(args.binary),'binary_sha256':hashlib.sha256(args.binary.read_bytes()).hexdigest(),'persistent':args.persistent,'mode':args.mode,'runs':[],'passed':False}
def save():args.output.write_text(json.dumps(report,indent=2)+'\n')
def run(site,role):
 cmd=[str(args.binary),str(site),role,'persistent' if args.persistent else 'single',args.mode]
 try:
  p=subprocess.run(cmd,capture_output=True,text=True,timeout=15)
  row=dict(site=site,role=role,exit=p.returncode,stdout=p.stdout,stderr=p.stderr)
 except subprocess.TimeoutExpired as e:row=dict(site=site,role=role,exit='timeout',stdout=str(e.stdout),stderr=str(e.stderr))
 report['runs'].append(row);save();return row
baseline=run(-1,'source')
if baseline['exit'] or 'reason=0' not in baseline['stdout']:raise SystemExit('baseline failed')
for role in (['source'] if args.mode=='cycle' else ['source','worker']):
 for site in range(2000):
  row=run(site,role)
  if row['exit']==12 and 'hit=0' in row['stdout']:
   print(role,site,'allocation sites exhausted',flush=True);break
  if row['exit']!=0 or 'hit=1' not in row['stdout']:
   print(row,flush=True);raise SystemExit('allocation containment failed')
 else:raise SystemExit('bounded campaign did not exhaust sites')
report['passed']=True;save();print('PASS',len(report['runs']),'runs including baseline and exhaustion controls')
