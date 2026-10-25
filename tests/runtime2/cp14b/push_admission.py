#!/usr/bin/env python3
"""Bound the statement-push arity guard to proven array/source-view shapes."""
import argparse,json,tempfile
from pathlib import Path
from functional import run

def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--base',required=True);p.add_argument('--output',required=True);a=p.parse_args();rows=[]
    contexts={'top':lambda b:b,'function':lambda b:'fn(f()) { '+b+' }; f()',
              'loop_function':lambda b:'fn(f()) { '+b+' }; i:=0; while(i<1) { i+=1; f() }'}
    with tempfile.TemporaryDirectory(prefix='cp14b-push-admission-') as folder:
        for receiver in ['"abc"','5','true','null','{"k":1}','[1,2]','[]']:
            for name,wrap in contexts.items():
                source=wrap('a:='+receiver+'; a.push(1,2)');root=Path(folder)
                legacy=run(a.base,'0',source,root);auto=run(a.nift,'1',source,root);vm=run(a.nift,'require',source,root)
                refused=not receiver.startswith('[') or name=='loop_function';st=vm['stats'][-1] if vm['stats'] else {}
                ok=all(auto[k]==legacy[k] for k in ['out','err','exit'])
                if refused:ok=ok and st.get('outcome')=='require-error' and st.get('reason')=='static-error:push-arity' and st.get('vm_executions')==0 and st.get('legacy_executions')==0 and not vm['out']
                else:ok=ok and st.get('outcome')=='vm-error' and st.get('vm_executions')==1 and st.get('legacy_executions')==0 and all(vm[k]==legacy[k] for k in ['out','err','exit'])
                rows.append(dict(source=source,refused=refused,pass_=bool(ok),legacy=legacy,automatic=auto,require=vm));print('PASS' if ok else 'FAIL',receiver,name)
    Path(a.output).write_text(json.dumps(rows,indent=2)+'\n');return 0 if all(r['pass_'] for r in rows) else 1
if __name__=='__main__':raise SystemExit(main())
