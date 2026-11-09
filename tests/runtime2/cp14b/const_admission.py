#!/usr/bin/env python3
"""Narrow const admission must not enable the older unguarded index setter."""
import argparse,json,tempfile
from pathlib import Path
from functional import run
# B5 (b5-refsem.md section 6, class i; owner Q2 option B, 2026-11-02): `const` is binding-only,
# so an element store through a const binding runs (legacy and the B4b tip: "cannot assign to
# const binding" / declare-const refusal). Each pinned to its exact B5 result.
Q2B=[
('const a:=[1,2]; a[0]=9; print(a.stringify())','[9,2]\n'),
('fn(f()) { const a:=[1,2]; a[0]=9; print(a.stringify()) }; f()','[9,2]\n'),
('fn(f()) { const a:=[1,2]; g:=()=>a[0]=9; g(); print(a.stringify()) }; f()','[9,2]\n'),
('print("SIDE"); const a:=[1,2]; a[0]=9','SIDE\n'),
]
PARITY=[
'const a:=[1,2]; for(v:a) { v=9 }; print(a.stringify())',
]
# B5: the Phase A guard is deleted; a mutation through the loop binding mutates the element
# (plan section 3 Iteration), legacy-equal ([[1,9],[2,9]]).
SHARED=[
'const a:=[[1],[2]]; for(v:a) { v.push(9) }; print(a.stringify())',
]
# Review of C47-C49 (combined-b3-b5.md section 44), part 3 of the binding-only const contract:
# values the language treats as immutable (strings, numbers; booleans and null have no operation
# that could change them in place) stay unchanged when a const binding's value is "changed" through
# an alias, a function parameter, a lambda or a collection element: each such change makes a new
# value. Each runs on the VM and equals legacy exactly. (Element stores and pushes on a string are
# errors in legacy and refusals or the same errors on the VM: they never change the string.)
IMMUTABLE=[
'const s:="ab"; t:=s; t+="c"; print(s); print(t)',
'const s:="ab"; t:=s.to_upper(); print(s); print(t)',
'fn(f(x)) { x+="c"; return x }; const s:="ab"; print(f(s)); print(s)',
'const s:="ab"; g:=(x)=>x+"c"; print(g(s)); print(s)',
'const s:="ab"; xs:=[s]; xs[0]=xs[0]+"c"; print(s); print(xs[0])',
'const n:=1; m:=n; m+=1; print(n); print(m)',
'const d:=2.5; e:=d; e*=2; print(d); print(e)',
]
def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--base',required=True);p.add_argument('--output',required=True);p.add_argument('--vm-only-harness',action='store_true');a=p.parse_args();rows=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-const-') as folder:
        root=Path(folder)
        for source,want in Q2B:
            legacy=run(a.base,'0',source,root);vm=run(a.nift,'require',source,root);st=vm['stats'][-1] if vm['stats'] else {}
            ok=(vm['exit']==0 and vm['out']==want and not vm['err'] and st.get('vm_executions')==1 and st.get('legacy_executions')==0 and legacy['exit']==1 and 'const' in legacy['err'])
            rows.append(dict(kind='Q2 B: const is binding-only (B5)',source=source,pass_=bool(ok),legacy=legacy,require=vm))
        for source in PARITY:
            legacy=run(a.base,'0',source,root);vm=run(a.nift,'require',source,root)
            ok=all(vm[k]==legacy[k] for k in ['out','err','exit']) and vm['stats'] and vm['stats'][-1]['vm_executions']==1 and vm['stats'][-1]['legacy_executions']==0
            rows.append(dict(kind='existing shallow const/for behavior',source=source,pass_=bool(ok),legacy=legacy,require=vm))
        for source in SHARED:
            legacy=run(a.base,'0',source,root);vm=run(a.nift,'require',source,root);st=vm['stats'][-1] if vm['stats'] else {}
            ok=all(vm[k]==legacy[k] for k in ['out','err','exit']) and st.get('vm_executions')==1 and st.get('legacy_executions')==0
            rows.append(dict(kind='shared loop element (B5, legacy-equal)',source=source,pass_=bool(ok),legacy=legacy,require=vm))
        for source in IMMUTABLE:
            legacy=run(a.base,'0',source,root);vm=run(a.nift,'require',source,root);st=vm['stats'][-1] if vm['stats'] else {}
            ok=(all(vm[k]==legacy[k] for k in ['out','err','exit']) and vm['exit']==0 and
                st.get('vm_executions')==1 and st.get('legacy_executions')==0)
            rows.append(dict(kind='immutable value through a const binding (B5 Q2B)',source=source,pass_=bool(ok),legacy=legacy,require=vm))
    Path(a.output).write_text(json.dumps(rows,indent=2)+'\n');print('PASS' if all(r['pass_'] for r in rows) else 'FAIL',len(rows),'const admission checks');return 0 if all(r['pass_'] for r in rows) else 1
if __name__=='__main__':raise SystemExit(main())
