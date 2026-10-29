#!/usr/bin/env python3
"""Keep CP22's historical goldens, and verify each CP23 admission with exact
execution/fallback counters. Four explicit differences remain review qualifications."""
import argparse,importlib.util,json,pathlib,sys
HERE=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('cp22',HERE.parent/'cp22/run.py');cp22=importlib.util.module_from_spec(spec);spec.loader.exec_module(cp22)
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--output',type=pathlib.Path,required=True);args=p.parse_args()
rows=[];failures=[]
qualified={'deep_chain.f':(b'1275\n',0),'r_share_closure_ret.f':(b'32\n12\n',0)}
for name,old,outcome,reason,policy in cp22.table(HERE/'CP22-CLASSIFICATION.tsv'):
 result,stats=cp22.run(str(pathlib.Path(args.nift).resolve()),cp22.REFUSE,name,'require',extra={'NIFT_VM_PARALLEL_WORKERS':'2'},switches=False)
 problems=[];reason='' if reason=='-' else reason
 if not stats:problems.append('missing statistics')
 else:
  if stats.get('outcome')!=outcome or stats.get('reason')!=reason:problems.append('classification mismatch')
  vm=0 if outcome=='require-error' else 1
  if stats.get('vm_executions')!=vm or stats.get('legacy_executions')!=0:problems.append('execution counters')
 if not isinstance(result[2],int) or result[2]<0:problems.append('harness failure')
 elif outcome=='vm' and result[2]!=0:problems.append('VM execution failed')
 elif outcome!='vm' and result[2]!=1:problems.append('error exit mismatch')
 if policy=='legacy-golden' and result!=cp22.golden(cp22.REFUSE,name):problems.append('legacy differential')
 if name in qualified and (result[0],result[2])!=qualified[name]:problems.append('qualified result drift')
 if name=='r_thread_arity.f' and b'thread: callable argument count mismatch: w' not in result[1]:problems.append('callee diagnostic drift')
 if name=='r_f3_member_print_default.f' and sorted(result[0].splitlines())!=[b'1',b'2',b'3',b'6']:problems.append('lost/duplicated output')
 row=dict(fixture=name,cp22_reason=old,policy=policy,stats=stats,exit=result[2],stdout=result[0].decode(errors='replace'),stderr=result[1].decode(errors='replace'),problems=problems)
 rows.append(row)
 if problems:failures.append(name);print(name,problems)
args.output.write_text(json.dumps(dict(rows=rows,failures=failures,qualifications=[r['fixture'] for r in rows if r['policy'].startswith('qualified:')]),indent=2)+'\n')
print(f'CP23 compatibility: {len(rows)} classifications, {len(failures)} failures; 4 explicit review qualifications')
raise SystemExit(bool(failures))
