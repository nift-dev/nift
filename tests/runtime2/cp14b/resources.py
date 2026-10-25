#!/usr/bin/env python3
"""Local CP14B resource/performance witnesses; no provisioning or spend."""
import argparse,hashlib,json,os,platform,statistics,subprocess,tempfile,time
from pathlib import Path

def source(method,n):
    expression={'map':'a.map(x=>x+1)','filter':'a.filter(x=>x%2==0)','sort_by':'a.sort_by(x=>0-x)'}[method]
    return f'a:=range({n})\nr:={expression}\nprint(r.size())\nprint(r[0])\n'
def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--legacy',required=True);p.add_argument('--output',required=True);p.add_argument('--samples',type=int,default=5);a=p.parse_args();records=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-resources-') as folder:
        root=Path(folder)
        for method in ['map','filter','sort_by']:
            for n in [100000,1000000]:
                for mode in (['0','require'] if n==100000 else ['require']):
                    for sample in range(a.samples if n==100000 else 1):
                        text=source(method,n);(root/'p.f').write_text(text);stats=root/'stats';stats.unlink(missing_ok=True)
                        env=dict(os.environ,NIFT_VM=mode,NIFT_PREVALIDATE='off',NIFT_VM_HEAP_BUDGET='128000000',NIFT_VM_STATS=str(stats))
                        binary=a.legacy if mode=='0' else a.nift;start=time.perf_counter_ns()
                        rss=root/'rss';proc=subprocess.run(['/usr/bin/time','-f','%M','-o',str(rss),binary,'p.f'],cwd=root,env=env,capture_output=True,text=True,timeout=60)
                        elapsed=(time.perf_counter_ns()-start)/1e9
                        want=f'{n//2 if method=="filter" else n}\n{n-1 if method=="sort_by" else 1 if method=="map" else 0}\n'
                        st=[json.loads(s) for s in stats.read_text().splitlines()] if stats.exists() else []
                        ok=proc.returncode==0 and proc.stdout==want and (mode=='0' or st and st[-1]['outcome']=='vm' and st[-1]['legacy_executions']==0)
                        records.append(dict(method=method,n=n,mode=mode,sample=sample,seconds=elapsed,rss_max_kib=int(rss.read_text().splitlines()[-1]),pass_=bool(ok),source=text,stdout=proc.stdout,stderr=proc.stderr,exit=proc.returncode,stats=st))
                        print(method,n,mode,sample,round(elapsed,6),'PASS' if ok else 'FAIL',flush=True)
        comparisons=[]
        for method in ['map','filter','sort_by']:
            times={mode:[r['seconds'] for r in records if r['method']==method and r['n']==100000 and r['mode']==mode] for mode in ['0','require']}
            comparisons.append(dict(method=method,target_met=statistics.median(times['require'])<statistics.median(times['0']),legacy_median=statistics.median(times['0']),vm_median=statistics.median(times['require']),ratio=statistics.median(times['require'])/statistics.median(times['0'])))
    report=dict(status='PASS' if all(r['pass_'] for r in records) else 'FAIL',environment=dict(platform=platform.platform(),processor=platform.processor(),cpu_count=os.cpu_count(),cpuinfo=Path('/proc/cpuinfo').read_text().split('\n\n')[0],compiler=subprocess.check_output(['g++','--version'],text=True),flags='-std=c++17 -O2 -Wall -Wextra -pedantic -pthread',clock='whole-process perf_counter_ns, including identical /usr/bin/time wrappers'),binaries={b:hashlib.sha256(Path(b).read_bytes()).hexdigest() for b in [a.nift,a.legacy]},records=records,comparisons=comparisons,limitation='Local shared machine; preliminary evidence only. Dedicated Linode comparison requires separate owner approval.')
    Path(a.output).write_text(json.dumps(report,indent=2)+'\n');return 0 if report['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
