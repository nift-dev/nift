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
    Path(a.output).write_text(json.dumps(rows,indent=2)+'\n');print('PASS' if all(r['pass_'] for r in rows) else 'FAIL',len(rows),'const admission checks');return 0 if all(r['pass_'] for r in rows) else 1
if __name__=='__main__':raise SystemExit(main())
