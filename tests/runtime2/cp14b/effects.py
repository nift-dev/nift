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
'aliased-receiver':('a:=[3,1,2]; b:=a; fn(cmp(x,y)) { b.push(9); return x<y }; a.sort(cmp); print(a.stringify())',False),
'disjoint-array':('a:=[3,1,2]; b:=[3,1,2]; fn(cmp(x,y)) { b.push(9); return x<y }; a.sort(cmp); print(a.stringify()); print(b.size() > 3)',False),
}
def record_sort(count, *, prefix="", callback="", missing_field=False):
    records = [{"key": value} for value in range(count, 0, -1)]
    if missing_field:
        records[-1] = {"other": 0}
    return (f"a := {json.dumps(records)}\n{prefix}"
            f"fn(cmp(x,y)) {{ {callback} print(x.stringify()); return x.key < y.key }}\n"
            'a.sort(cmp); print(a.stringify())')

CASES.update({
    "record-boundary-16": (record_sort(16), False),
    "record-boundary-17": (record_sort(17), False),
    "record-alias-before-sort": (record_sort(17, prefix="b := a\n"), True),
    "record-write-in-comparator": (record_sort(17, callback="x.key = 0;"), True),
    "record-missing-field": (record_sort(17, missing_field=True), True),
})

def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--legacy',required=True);p.add_argument('--output',required=True);a=p.parse_args();results=[]
    import comparator_profile as cprof
    prof=cprof.profile(a.nift);unverified=0
    with tempfile.TemporaryDirectory(prefix='cp14b-effects-') as folder:
        root=Path(folder)
        for name,(source,refusal) in CASES.items():
            # On a refused profile a D5 refusal cannot be told from the toolchain refusal: unverified.
            if prof.refused and cprof.is_comparator(source):
                ok,detail=cprof.refused_ok(prof,a.nift,a.legacy,source,root);unverified+=1
                results.append(dict(name=name,source=source,pass_=bool(ok),coverage='unverified-comparator',detail=detail));print('UNVERIFIED' if ok else 'FAIL',name);continue
            vm=run(a.nift,'require',source,root)
            if refusal:
                st=vm['stats'][-1] if vm['stats'] else {}
                ok=vm['exit']==1 and not vm['out'] and st.get('reason')=='method:sort' and st.get('vm_executions')==0 and st.get('legacy_executions')==0
                legacy=None
            elif name=='aliased-receiver':
                # Preserve the original input. Under B2, b aliases the receiver
                # and its mutation is blocked while the comparator is running.
                legacy=None
                st=vm['stats'][-1] if vm['stats'] else {}
                ok=(vm['exit']==1 and not vm['out'] and st.get('outcome')=='vm-error'
                    and st.get('code')=='runtime.sort_receiver_mutated' and st.get('catchable') is True
                    and st.get('vm_executions')==1 and st.get('legacy_executions')==0)
            else:
                legacy=run(a.legacy,'0',source,root);ok=all(vm[k]==legacy[k] for k in ['out','err','exit'])
            if name.startswith("record-boundary-"):
                st=vm['stats'][-1] if vm['stats'] else {}
                ok=(ok and vm['exit']==0 and st.get('outcome')=='vm'
                    and st.get('vm_executions')==1 and st.get('legacy_executions')==0)
            if name=='disjoint-array':
                st=vm['stats'][-1] if vm['stats'] else {}
                ok=(ok and vm['exit']==0 and vm['out']=='[1,2,3]\ntrue\n'
                    and st.get('vm_executions')==1 and st.get('legacy_executions')==0)
            results.append(dict(name=name,source=source,pass_=bool(ok),vm=vm,legacy=legacy));print('PASS' if ok else 'FAIL',name)
    print(cprof.banner(prof,unverified,'effects'))
    Path(a.output).write_text(json.dumps(results,indent=2)+'\n');return 0 if all(r['pass_'] for r in results) else 1
if __name__=='__main__':raise SystemExit(main())
