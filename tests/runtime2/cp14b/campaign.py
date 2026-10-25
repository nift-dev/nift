#!/usr/bin/env python3
"""Prepared CP14B measurement protocol; runs supplied binaries, never provisions."""
import argparse,hashlib,json,os,random,statistics,subprocess,tempfile,time
from pathlib import Path

def source(method,n,reps):
    expression={'map':'a.map(x=>x+1)','filter':'a.filter(x=>x%2==0)','sort_by':'a.sort_by(x=>0-x)'}[method]
    return f'a:=range({n})\ni:=0\nr:=[]\nwhile(i<{reps}) {{ r={expression}; i+=1 }}\nprint(r.size())\nprint(r[0])\n'
def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--legacy',required=True);p.add_argument('--output',required=True);p.add_argument('--samples',type=int,default=31);p.add_argument('--n',type=int,default=100000);a=p.parse_args()
    if a.samples<2 or a.n<2:p.error('samples and n must be at least 2')
    records=[];rng=random.Random(20261025)
    with tempfile.TemporaryDirectory(prefix='cp14b-campaign-') as folder:
        root=Path(folder)
        def measure(method,mode,reps,phase,sample):
            text=source(method,a.n,reps);(root/'p.f').write_text(text);stats=root/'stats';stats.unlink(missing_ok=True)
            binary=a.legacy if mode=='0' else a.nift
            env=dict(os.environ,NIFT_VM=mode,NIFT_PREVALIDATE='off',NIFT_VM_STATS=str(stats),NIFT_VM_HEAP_BUDGET='128000000')
            start=time.perf_counter_ns()
            with (root/'out').open('wb') as out,(root/'err').open('wb') as err:
                child=subprocess.Popen([binary,'p.f'],cwd=root,env=env,stdout=out,stderr=err,stdin=subprocess.DEVNULL)
                _,status,usage=os.wait4(child.pid,0);child.returncode=os.waitstatus_to_exitcode(status)
            wall=(time.perf_counter_ns()-start)/1e9;stdout=(root/'out').read_bytes();stderr=(root/'err').read_bytes()
            st=[json.loads(line) for line in stats.read_text().splitlines()] if stats.exists() else []
            want=f'{a.n//2 if method=="filter" else a.n}\n{a.n-1 if method=="sort_by" else 1 if method=="map" else 0}\n'.encode()
            ok=child.returncode==0 and stdout==want and not stderr and (mode=='0' or st and st[-1]['outcome']=='vm' and st[-1]['legacy_executions']==0)
            row=dict(method=method,mode=mode,reps=reps,phase=phase,sample=sample,wall_seconds=wall,cpu_seconds=usage.ru_utime+usage.ru_stime,rss_max_kib=usage.ru_maxrss,source=text,stdout=stdout.decode(),stderr=stderr.decode(errors='replace'),output_sha256=hashlib.sha256(stdout).hexdigest(),exit=child.returncode,stats=st,pass_=bool(ok));records.append(row)
            Path(a.output).write_text(json.dumps(dict(status='RUNNING' if ok else 'FAIL',records=records),indent=2)+'\n')
            if not ok:raise RuntimeError(json.dumps(row))
            return wall
        for method in ['map','filter','sort_by']:
            for mode in ['0','require']:measure(method,mode,1,'cold',0)
            reps=1
            while min(measure(method,mode,reps,'calibration',0) for mode in ['0','require'])<.08:
                reps*=2
                if reps>4096:raise RuntimeError('calibration bound exceeded')
            for sample in range(a.samples):
                modes=['0','require'];rng.shuffle(modes)
                for mode in modes:measure(method,mode,reps,'steady',sample)
    comparisons=[]
    for method in ['map','filter','sort_by']:
        values={m:[r['wall_seconds'] for r in records if r['method']==method and r['phase']=='steady' and r['mode']==m] for m in ['0','require']}
        ratios=[statistics.median(rng.choices(values['require'],k=a.samples))/statistics.median(rng.choices(values['0'],k=a.samples)) for _ in range(2000)];ratios.sort()
        comparisons.append(dict(method=method,legacy_median=statistics.median(values['0']),vm_median=statistics.median(values['require']),ratio=statistics.median(values['require'])/statistics.median(values['0']),bootstrap_ratio_95=[ratios[49],ratios[1949]]))
    report=dict(status='PASS',protocol='Cold separate; identical calibrated >=80ms steady work; balanced randomized order; no cross-method pooling',binaries={str(Path(b).resolve()):hashlib.sha256(Path(b).read_bytes()).hexdigest() for b in [a.nift,a.legacy]},records=records,comparisons=comparisons)
    Path(a.output).write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
