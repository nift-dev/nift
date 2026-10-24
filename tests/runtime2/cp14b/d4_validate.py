#!/usr/bin/env python3
"""Bounded D4 go/no-go; Python continuation is test-only, not a VM controller."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import random
import resource
import subprocess
import tempfile

def limits():
    resource.setrlimit(resource.RLIMIT_AS,(512<<20,512<<20))
    resource.setrlimit(resource.RLIMIT_FSIZE,(1<<20,1<<20))
    resource.setrlimit(resource.RLIMIT_CPU,(3,3))
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))

def move(src,i,dst,j):
    value=src[i]
    src[i]={} if isinstance(value,dict) else value
    dst[j]=value

def continuation(values,records,fail,desc,inner):
    a=[dict(k=v,id=i) for i,v in enumerate(values)] if records else values.copy()
    buf=[None]*((len(a)+1)//2)
    trace=[]
    def compare(l,r):
        return (yield copy.deepcopy(dict(l=l,r=r,a=a)))
    def insertion(lo,hi):
        for i in range(lo+1,hi):
            if (yield from compare(a[i],a[lo])):
                tmp=[None];move(a,i,tmp,0)
                for j in range(i,lo,-1):move(a,j-1,a,j)
                move(tmp,0,a,lo)
            else:
                tmp=[None];move(a,i,tmp,0);j=i-1;hole=i
                while (yield from compare(tmp[0],a[j])):
                    move(a,j,a,hole);hole=j;j-=1
                move(tmp,0,a,hole)
    def merge(src,lo,mid,hi,dst,start):
        i,j=lo,mid
        while i<mid and j<hi:
            if (yield from compare(src[j],src[i])):move(src,j,dst,start);j+=1
            else:move(src,i,dst,start);i+=1
            start+=1
        while i<mid:move(src,i,dst,start);i+=1;start+=1
        while j<hi:move(src,j,dst,start);j+=1;start+=1
    def pass_merge(src,lo,hi,dst,start,step):
        while hi-lo>=2*step:
            yield from merge(src,lo,lo+step,lo+2*step,dst,start)
            start+=2*step;lo+=2*step
        yield from merge(src,lo,min(lo+step,hi),hi,dst,start)
    def half(lo,hi):
        at=lo
        while hi-at>=7:yield from insertion(at,at+7);at+=7
        yield from insertion(at,hi)
        step=7
        while step<hi-lo:
            yield from pass_merge(a,lo,hi,buf,0,step);step*=2
            yield from pass_merge(buf,0,hi-lo,a,lo,step);step*=2
    def sort():
        n=len(a);mid=(n+1)//2
        yield from half(0,mid)
        yield from half(mid,n)
        # Full-buffer adaptive merge: left buffered for even n, right for odd n.
        if mid<=n-mid:
            for i in range(mid):move(a,i,buf,i)
            i,j,k=0,mid,0
            while i<mid and j<n:
                if (yield from compare(a[j],buf[i])):move(a,j,a,k);j+=1
                else:move(buf,i,a,k);i+=1
                k+=1
            while i<mid:move(buf,i,a,k);i+=1;k+=1
        else:
            for i in range(n-mid):move(a,mid+i,buf,i)
            i,j,k=mid-1,n-mid-1,n
            while i>=0 and j>=0:
                k-=1
                if (yield from compare(buf[j],a[i])):move(a,i,a,k);i-=1
                else:move(buf,j,a,k);j-=1
            while j>=0:k-=1;move(buf,j,a,k);j-=1
    driver=sort();failed=False;calls=0
    try:
        request=next(driver)
        while True:
            if failed:answer=False
            else:
                trace.append(request);calls+=1
                if calls==fail and not inner:failed=True;answer=False
                else:
                    l,r=request['l'],request['r'];l=l['k'] if records else l;r=r['k'] if records else r
                    answer=l>r if desc else l<r
            request=driver.send(answer)
    except StopIteration:pass
    return dict(trace=trace,final=a,calls=calls,failed=failed)

def manifest():
    cases=[]
    for n in [0,1,2,3,6,7,8,13,14,15,16,17,31,32,33]:
        rng=random.Random(140026+n);shuffled=list(range(n));rng.shuffle(shuffled)
        for name,values in [('asc',list(range(n))),('desc',list(range(n-1,-1,-1))),('equal',[1]*n),('dups',[i%3 for i in range(n)]),('seed',shuffled)]:
            for records in [False,True]:
                cases.append(dict(id=f'{n}-{name}-{int(records)}',values=values,records=records,fail=0,desc=False,inner=False))
    # Failure positions derive deterministically from the successful model.
    for n in [3,7,8,15,17,33]:
        values=list(range(n-1,-1,-1))
        for records in [False,True]:
            count=continuation(values,records,0,False,False)['calls']
            points=range(1,count+1) if n<=8 else sorted({1,2,count//2,count})
            for fail in points:
                cases.append(dict(id=f'fail-{n}-{int(records)}-{fail}',values=values,records=records,fail=fail,desc=False,inner=False))
    for n in [3,8,17,33]:
        for records in [False,True]:
            cases.append(dict(id=f'reverse-{n}-{int(records)}',values=list(range(n)),records=records,fail=0,desc=True,inner=False))
            cases.append(dict(id=f'inner-{n}-{int(records)}',values=list(range(n-1,-1,-1)),records=records,fail=2,desc=False,inner=True))
    return cases

def nift_source(case):
    values=[dict(k=v,id=i) for i,v in enumerate(case['values'])] if case['records'] else case['values']
    key='x.k' if case['records'] else 'x';rkey='y.k' if case['records'] else 'y'
    inner='try { throw error("inside") } catch(e) { }' if case['inner'] else 'throw error("stop")'
    return ('a := '+json.dumps(values)+'\nn := 0\nfn(cmp(x,y)) {\n'
            ' print("T" + x.stringify() + "|" + y.stringify() + "|" + a.stringify())\n'
            ' n += 1\n'+(f' if(n == {case["fail"]}) {{ {inner} }}\n' if case['fail'] else '')+
            f' return {key} '+('>' if case['desc'] else '<')+f' {rkey}\n}}\n'
            'try { a.sort(cmp) } catch(e) { print("E" + e.message) }\n'
            'print("A" + a.stringify())\nprint("N" + n.to_string())\n')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--oracle',required=True);ap.add_argument('--nift',required=True);ap.add_argument('--output',required=True);args=ap.parse_args()
    cases=manifest();assert len(cases)<500
    observations=[]
    for c in cases:
        inputs=f'{int(c["records"])} {len(c["values"])} {c["fail"]} {int(c["desc"])} {int(c["inner"])} '+' '.join(map(str,c['values']))+'\n'
        native=subprocess.run([args.oracle],input=inputs,text=True,capture_output=True,timeout=5,preexec_fn=limits)
        if native.returncode or len(native.stdout)>1<<20:raise RuntimeError(f'Native harness failure: {native.returncode} {native.stderr}')
        observations.append(json.loads(native.stdout))
    results=[];requests=0
    for c,want in zip(cases,observations):
        got=continuation(**{k:c[k] for k in ['values','records','fail','desc','inner']})
        requests+=got['calls']+want['calls'];assert requests<=65536
        results.append(dict(case=c,match=got==want,model=got,native=want))
    # Actual legacy CLI triangulation, including throw/inner catch and records.
    selected=[c for c in cases if c['id'] in {'3-desc-0','3-desc-1','8-dups-0','8-dups-1','17-seed-0','17-seed-1','fail-3-0-2','fail-3-1-2','fail-17-0-2','inner-8-0','inner-17-1','reverse-8-0'}]
    cli=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-d4-') as folder:
        for c in selected:
            source=nift_source(c);Path(folder,'p.f').write_text(source)
            p=subprocess.run([args.nift,str(Path(folder,'p.f'))],env=dict(os.environ,NIFT_VM='0',NIFT_PREVALIDATE='off'),capture_output=True,text=True,timeout=5,preexec_fn=limits)
            if p.returncode:raise RuntimeError(f'CLI harness failure {c["id"]}: {p.returncode} {p.stderr}')
            trace=[];final=None;count=None;failed=False
            for line in p.stdout.splitlines():
                if line.startswith('T'):
                    l,r,a=map(json.loads,line[1:].split('|'));trace.append(dict(l=l,r=r,a=a))
                elif line.startswith('A'):final=json.loads(line[1:])
                elif line.startswith('N'):count=int(line[1:])
                elif line=='Estop':failed=True
                else:raise RuntimeError(f'Unexpected CLI output: {line!r}')
            got=dict(trace=trace,final=final,calls=count,failed=failed);requests+=count
            want=next(r['native'] for r in results if r['case']['id']==c['id'])
            cli.append(dict(case=c['id'],source=source,exit=p.returncode,stdout=p.stdout,stderr=p.stderr,match=got==want))
    # Fatal callback/contract faults are terminal: compare only observable
    # callback prefix and absence of post-sort output, not inaccessible state.
    with tempfile.TemporaryDirectory(prefix='cp14b-d4-fatal-') as folder:
        c=next(c for c in cases if c['id']=='fail-3-0-2')
        expected=next(r['native']['trace'] for r in results if r['case']['id']==c['id'])
        for name,replacement,message in [('nonbool','return 1','sort: comparator must return bool'),('arithmetic','return 1 / 0','division by zero')]:
            source=nift_source(c).replace('throw error("stop")',replacement)
            Path(folder,'p.f').write_text(source)
            p=subprocess.run([args.nift,str(Path(folder,'p.f'))],env=dict(os.environ,NIFT_VM='0',NIFT_PREVALIDATE='off'),capture_output=True,text=True,timeout=5,preexec_fn=limits)
            trace=[]
            for line in p.stdout.splitlines():
                if not line.startswith('T'):raise RuntimeError('terminal failure ran post-sort output')
                l,r,a=map(json.loads,line[1:].split('|'));trace.append(dict(l=l,r=r,a=a))
            requests+=len(trace)
            cli.append(dict(case=name,source=source,exit=p.returncode,stdout=p.stdout,stderr=p.stderr,match=p.returncode==1 and message in p.stderr and trace==expected))
    assert requests<=65536 and len(cli)+len(cases)<=512
    record=dict(status='PASS' if all(r['match'] for r in results+cli) else 'MISMATCH',cases=len(cases),child_runs=len(cli)+len(cases),callback_requests=requests,nift_sha256=hashlib.sha256(Path(args.nift).read_bytes()).hexdigest(),results=results,cli=cli)
    Path(args.output).write_text(json.dumps(record,indent=2)+'\n')
    print(record['status'],record['cases'],'model/native cases;',len(cli),'legacy CLI checks;',requests,'callback requests')
    for r in results:
        if not r['match']:print('MISMATCH',r['case']['id'])
    for r in cli:
        if not r['match']:print('CLI MISMATCH',r['case'])
    return 0 if record['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
