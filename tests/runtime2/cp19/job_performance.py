"""Matched CP21 foundation/CP19/CP18 instruction evidence; unchanged 1M-catch workloads."""
import argparse,hashlib,json,os,platform,random,re,shutil,statistics,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--rounds',type=int,default=5);p.add_argument('--reuse-reference',type=Path);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True)
base=Path('/home/nick/Repositories/nift/.cp19-development/complete');new=Path.cwd();accepted=new.parent/'accepted-baseline'
binaries={'default':{'cp18':base/'performance-1c0b157f/base/nift','foundation':accepted/'nift','old_':Path('/home/nick/Repositories/nift/.cp19-reconciliation/delivery/candidate-5f653128/binaries/nift-default'),'new_':new/'nift'},'switch':{'cp18':base/'assessment-1c0b157f/base-switch-nift','foundation':accepted/'.build/cp13b/nift-switch','old_':Path('/home/nick/Repositories/nift/.cp19-reconciliation/delivery/candidate-5f653128/binaries/nift-switch'),'new_':new/'.build/cp13b/nift-switch'}}
work=Path('/tmp/cp19-matched-origin/work');work.mkdir(parents=True,exist_ok=True);runner=work/'runner';stats=work/'stats.jsonl';rows=[];rng=random.Random(192817)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
identity={dispatch:{name:dict(path=str(path),sha256=sha(path)) for name,path in items.items()} for dispatch,items in binaries.items()}
assert identity['default']['cp18']['sha256']=='82ee8793e5e2a4fc07dc5eed35bee045887c8339ef0dac1facd406394eebb9a9'
assert identity['switch']['cp18']['sha256']=='dbf276b11172935633947abef4ea9090ef57371af8c3150c1fa67c374f0081b3'
if a.reuse_reference:
 prior=json.loads(a.reuse_reference.read_text())
 for row in prior:
  assert row['source_path']==str(work/'p.f')
  if row['binary']!='new_':
   assert row['sha256']==identity[row['dispatch']][row['binary']]['sha256'];rows.append({**row,'reference_evidence':str(a.reuse_reference.resolve())})
 assert len(rows)==12*a.rounds
(a.out/'metadata.json').write_text(json.dumps(dict(platform=platform.platform(),valgrind=subprocess.check_output(['valgrind','--version'],text=True).strip(),binaries=identity,conditions='Identical cwd, executable spelling, source path, HOME, workload bytes, VM limits; no parallel heavy campaign. Same threshold <=1.0, no waiver.',source_origin=str(work/'p.f'),rounds=a.rounds),indent=2)+'\n')
for round in range(a.rounds):
 lanes=[(dispatch,workload,name) for dispatch in binaries for workload in ['caught','caught_cell'] for name in binaries[dispatch] if not a.reuse_reference or name=='new_'];rng.shuffle(lanes)
 for dispatch,workload,name in lanes:
  source=Path(f'tests/runtime2/cp13b/stress/stress_{workload}.f.in').read_text().replace('@COUNT@','1000000');(work/'p.f').write_text(source)
  folder=a.out/f'r{round}'/dispatch/workload/name;folder.mkdir(parents=True)
  (folder/'source.f').write_text(source);runner.unlink(missing_ok=True);shutil.copy2(binaries[dispatch][name],runner);assert sha(runner)==identity[dispatch][name]['sha256']
  stats.unlink(missing_ok=True);profile=folder/'callgrind.out'
  env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=str(work),NIFT_VM='require',NIFT_PREVALIDATE='off',NIFT_VM_STATS=str(stats),NIFT_VM_HEAP_BUDGET='8000000')
  argv=['valgrind','--tool=callgrind','--callgrind-out-file='+str(profile),str(runner),'p.f']
  r=subprocess.run(argv,cwd=work,env=env,stdin=subprocess.DEVNULL,text=True,capture_output=True,timeout=900)
  (folder/'stdout.txt').write_text(r.stdout);(folder/'stderr.txt').write_text(r.stderr)
  counters=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
  if stats.exists():shutil.copy2(stats,folder/'stats.jsonl')
  match=re.search(r'^summary: (\d+)',profile.read_text(),re.M) if profile.exists() else None
  row=dict(round=round,dispatch=dispatch,workload=workload,binary=name,sha256=identity[dispatch][name]['sha256'],source_sha256=hashlib.sha256(source.encode()).hexdigest(),source_path=str(work/'p.f'),status=r.returncode,stdout=r.stdout,instructions=int(match[1]) if match else None,stats=counters,argv=argv);rows.append(row);(a.out/'samples.json').write_text(json.dumps(rows,indent=2)+'\n')
  assert r.returncode==0 and r.stdout=='ok\n' and len(counters)==1 and counters[0]['outcome']=='vm' and counters[0]['vm_executions']==1 and counters[0]['legacy_executions']==0,row
  print(round,dispatch,workload,name,row['instructions'],flush=True)
summary=[]
for dispatch in binaries:
 for workload in ['caught','caught_cell']:
  samples={name:[r['instructions'] for r in rows if r['dispatch']==dispatch and r['workload']==workload and r['binary']==name] for name in binaries[dispatch]}
  med={k:statistics.median(v) for k,v in samples.items()}
  summary.append(dict(dispatch=dispatch,workload=workload,samples=samples,medians=med,cp19_increment_vs_accepted_foundation=med['new_']/med['foundation'],accepted_foundation_vs_cp18=med['foundation']/med['cp18'],old_cp19_vs_cp18=med['old_']/med['cp18'],new_vs_cp18=med['new_']/med['cp18'],threshold=1.0,waiver=False))
for entry in summary:
 foundation=entry['samples']['foundation']; candidate=entry['samples']['new_']; cp18=entry['samples']['cp18']
 rng_ci=random.Random(192819)
 for label,reference in [('vs_cp18',cp18),('increment_vs_foundation',foundation)]:
  ratios=[]
  for _ in range(10000):
   indices=[rng_ci.randrange(len(candidate)) for _ in candidate]
   ratios.append(statistics.median([candidate[i] for i in indices])/statistics.median([reference[i] for i in indices]))
  ratios.sort();entry[label+'_paired_bootstrap_95']=[ratios[249],ratios[9749]]
 entry['pass']=entry['new_vs_cp18']<=entry['threshold']
(a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print('Recorded matched instruction attribution; compiler timing is a separate question',flush=True)
