#!/usr/bin/env python3
"""D5 pre-execution refusals and disjoint/recursive positive witnesses."""
import argparse,json,os,tempfile
from pathlib import Path
from functional import run
CASES={
'self-structural':('a:=[2,1]; print("SIDE"); a.sort((x,y)=>a.push(3)<y)',True),
'receiver-write':('a:=[2,1]; print("SIDE"); fn(cmp(x,y)) { a=[1]; return x<y }; a.sort(cmp)',True),
'unknown-effects':('a:=[2,1]; print("SIDE"); fn(cmp(x,y)) { for(v:a) { v+=1 }; return x<y }; a.sort(cmp)',True),
'exact-number-profile':('a:=[9007199254740993,9007199254740992]; print("SIDE"); a.sort((x,y)=>x<y)',True),
'decimal-profile':('a:=[-1,0.0,2]; print("SIDE"); a.sort((x,y)=>x<y)',True),
'error-profile':('a:=[error("a"),error("b")]; print("SIDE"); a.sort((x,y)=>false)',True),
'disjoint-counter':('a:=[3,1,2]; n:=0; fn(cmp(x,y)) { n+=1; return x<y }; a.sort(cmp); print(a.stringify()); print(n)',False),
'safe-helper':('fn(less(x,y)) { return x<y }; a:=[3,1,2]; a.sort((x,y)=>less(x,y)); print(a.stringify())',False),
'safe-recursive-helper':('fn(less(x,y,n)) { if(n>0) { return less(x,y,n-1) }; return x<y }; a:=[3,1,2]; a.sort((x,y)=>less(x,y,2)); print(a.stringify())',False),
'recursive-same-site':('fn(work(n)) { a:=[3,2,1]; a.sort((x,y)=>{ if(n>0) { work(n-1) }; return x<y }); print(a.stringify()); return true }; work(2)',False),
'future-boundary':('fn(boom(x)) { throw error("stop") }; try { [1].unique_by(boom) } catch(e) { print(e.message) }; a:=[2,1]; a.sort((x,y)=>x<y); print(a.stringify())',False),
'completed-boundary':('a:=[2,1]; a.sort((x,y)=>x<y); fn(boom(x)) { throw error("stop") }; try { [1].unique_by(boom) } catch(e) { print(e.message) }; print(a.stringify())',False),
'disjoint-value-copy':('a:=[3,1,2]; b:=a; fn(cmp(x,y)) { b.push(9); return x<y }; a.sort(cmp); print(a.stringify())',False),
}
def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--legacy',required=True);p.add_argument('--output',required=True);a=p.parse_args();results=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-effects-') as folder:
        root=Path(folder)
        for name,(source,refusal) in CASES.items():
            vm=run(a.nift,'require',source,root)
            if refusal:
                st=vm['stats'][-1] if vm['stats'] else {}
                ok=vm['exit']==1 and not vm['out'] and st.get('reason')=='method:sort' and st.get('vm_executions')==0 and st.get('legacy_executions')==0
                legacy=None
            elif name=='disjoint-value-copy':
                # D-040 copies are independent; legacy aliases are not executed
                # here because mutating that alias would invalidate its sort.
                legacy=None;ok=vm['exit']==0 and vm['out']=='[1,2,3]\n'
            else:
                legacy=run(a.legacy,'0',source,root);ok=all(vm[k]==legacy[k] for k in ['out','err','exit'])
            results.append(dict(name=name,source=source,pass_=bool(ok),vm=vm,legacy=legacy));print('PASS' if ok else 'FAIL',name)
    Path(a.output).write_text(json.dumps(results,indent=2)+'\n');return 0 if all(r['pass_'] for r in results) else 1
if __name__=='__main__':raise SystemExit(main())
