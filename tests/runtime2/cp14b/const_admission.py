#!/usr/bin/env python3
"""Narrow const admission must not enable the older unguarded index setter."""
import argparse,json,tempfile
from pathlib import Path
from functional import run
REFUSALS=[
'const a:=[1,2]; a[0]=9; print(a.stringify())',
'fn(f()) { const a:=[1,2]; a[0]=9; print(a.stringify()) }; f()',
'fn(f()) { const a:=[1,2]; g:=()=>a[0]=9; g(); print(a.stringify()) }; f()',
'print("SIDE"); const a:=[1,2]; a[0]=9',
]
PARITY=[
'const a:=[1,2]; for(v:a) { v=9 }; print(a.stringify())',
'const a:=[[1],[2]]; for(v:a) { v.push(9) }; print(a.stringify())',
]
def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--base',required=True);p.add_argument('--output',required=True);p.add_argument('--vm-only-harness',action='store_true');a=p.parse_args();rows=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-const-') as folder:
        root=Path(folder)
        for source in REFUSALS:
            legacy=run(a.base,'0',source,root);vm=run(a.nift,'require',source,root);auto=None if a.vm_only_harness else run(a.nift,'1',source,root);st=vm['stats'][-1] if vm['stats'] else {}
            if a.vm_only_harness:
                # This driver has no fallback or CLI refusal renderer. Verify
                # compile-time refusal and no execution; automatic routing is
                # independently required on the real CLI in both modes.
                ok=(vm['exit']==2 and not vm['out'] and vm['err']=='not compiled: declare-const\n' and st.get('outcome')=='require-error' and st.get('vm_executions')==0 and st.get('legacy_executions')==0)
            else:ok=(vm['exit']==1 and not vm['out'] and st.get('reason')=='declare-const' and st.get('vm_executions')==0 and st.get('legacy_executions')==0 and all(auto[k]==legacy[k] for k in ['out','err','exit']))
            rows.append(dict(kind='retained pre-execution refusal',source=source,pass_=bool(ok),legacy=legacy,require=vm,automatic=auto))
        for source in PARITY:
            legacy=run(a.base,'0',source,root);vm=run(a.nift,'require',source,root)
            ok=all(vm[k]==legacy[k] for k in ['out','err','exit']) and vm['stats'] and vm['stats'][-1]['vm_executions']==1 and vm['stats'][-1]['legacy_executions']==0
            rows.append(dict(kind='existing shallow const/for behavior',source=source,pass_=bool(ok),legacy=legacy,require=vm))
    Path(a.output).write_text(json.dumps(rows,indent=2)+'\n');print('PASS' if all(r['pass_'] for r in rows) else 'FAIL',len(rows),'const admission checks');return 0 if all(r['pass_'] for r in rows) else 1
if __name__=='__main__':raise SystemExit(main())
