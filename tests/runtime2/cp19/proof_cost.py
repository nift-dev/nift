import argparse,hashlib,json,os,platform,random,statistics,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--baseline',required=True);p.add_argument('--baseline-switch');p.add_argument('--nift',required=True);p.add_argument('--switch',required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--samples',type=int,default=11);p.add_argument('--units',type=int,default=400);a=p.parse_args();rows=[];rng=random.Random(1927)
binaries={'baseline':Path(a.baseline).resolve(),'default':Path(a.nift).resolve(),'switch':Path(a.switch).resolve()}
if a.baseline_switch:binaries['baseline-switch']=Path(a.baseline_switch).resolve()
def save():a.out.write_text(json.dumps(dict(platform=platform.platform(),qualification='Local compiler cost; no acceptance waiver or portable timing certification',binaries={k:dict(path=str(v),sha256=hashlib.sha256(v.read_bytes()).hexdigest()) for k,v in binaries.items()},samples=rows),indent=2)+'\n')
a.out.parent.mkdir(parents=True,exist_ok=True)
for sample in range(-1,a.samples):
 for workload in ['stateless','unrelated','related']:
  names=list(binaries);rng.shuffle(names)
  for name in names:
   with tempfile.TemporaryDirectory(prefix='cp19-proof-cost-') as td:
    folder=Path(td);stats=folder/'stats';env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM='require',NIFT_VM_STATS=str(stats),NO_COLOR='1')
    if workload=='stateless':
     (folder/'p.f').write_text('a:=1\ni:=0\nwhile(i<1000){a+=1;i+=1}\nprint(a)\n');argv=[str(binaries[name]),'p.f'];source=None;expected='1001\n';count=1
    else:
     seed='a:=[1];tick:=0;fn(g(p)){return p(0)+a[0]};warm:=g(v=>v+1)\n'
     source=seed+('(tick+=1)\n' if workload=='unrelated' else 'g(v=>v+1)\n')*a.units+'exit\n';argv=[str(binaries[name]),'-i'];expected=''.join(str(k)+'\n' for k in range(1,a.units+1)) if workload=='unrelated' else '2\n'*a.units;count=a.units+1
    r=subprocess.run(argv,cwd=folder,env=env,input=source,text=True,capture_output=True,timeout=60)
    counters=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
    row=dict(sample=sample,workload=workload,binary=name,status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=counters);rows.append(row);save()
    assert r.returncode==0 and r.stdout==expected and not r.stderr and len(counters)==count,row
    assert all(c['outcome']=='vm' and c['vm_executions']==1 and c['legacy_executions']==0 for c in counters),row
summary=[]
for workload in ['stateless','unrelated','related']:
 for name in binaries:
  samples=[statistics.median(c['compile_us'] for c in (r['stats'] if workload=='stateless' else r['stats'][1:])) for r in rows if r['sample']>=0 and r['workload']==workload and r['binary']==name]
  bootstrap=sorted(statistics.median(rng.choices(samples,k=len(samples))) for _ in range(3000))
  summary.append(dict(workload=workload,binary=name,compile_us_median=statistics.median(samples),ci95=[bootstrap[75],bootstrap[2924]],individual_medians=samples))
(a.out.parent/(a.out.stem+'-summary.json')).write_text(json.dumps(summary,indent=2)+'\n')
print('PASS compiler-cost raw samples and bootstrap median intervals; all lanes affirm VM execution')

comparisons=[]
for dispatch,base in [('default','baseline'),('switch','baseline-switch')]:
 if base not in binaries:continue
 for workload in ['stateless','unrelated','related']:
  old=next(r for r in summary if r['binary']==base and r['workload']==workload)
  new=next(r for r in summary if r['binary']==dispatch and r['workload']==workload)
  ratios=[]
  for _ in range(3000):
   ids=rng.choices(range(a.samples),k=a.samples)
   ratios.append(statistics.median([new['individual_medians'][i] for i in ids])/statistics.median([old['individual_medians'][i] for i in ids]))
  ratios.sort()
  comparisons.append(dict(dispatch=dispatch,workload=workload,baseline=base,ratio=new['compile_us_median']/old['compile_us_median'],paired_bootstrap_ci95=[ratios[75],ratios[2924]],waiver=False))
(a.out.parent/(a.out.stem+'-comparisons.json')).write_text(json.dumps(comparisons,indent=2)+'\n')
