#!/usr/bin/env python3
"""Comparator sort is a separate 26th entry beyond the old built-in matrix."""
import argparse,json,sys,tempfile
from pathlib import Path
from functional import run
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'builtins'))
from context_probe import CONTEXTS,program

def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--base',required=True);p.add_argument('--output',required=True);p.add_argument('--vm-only-harness',action='store_true');a=p.parse_args();rows=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-comparator-contexts-') as folder:
        forms=[('inline',['n := [3,1,2]'],'n.sort((x,y)=>x<y)'),
               ('declared',['n := [3,1,2]','cmp := (x,y)=>x<y'],'n.sort(cmp)'),
               ('named',['fn(cmp(x,y)) { return x<y }','n := [3,1,2]'],'n.sort(cmp)'),
               ('factory',['fn(mk()) { return (x,y)=>x<y }','n := [3,1,2]'],'n.sort(mk())')]
        for name,setup,expr in forms:
          for context in CONTEXTS:
            source=program(setup,expr,context);root=Path(folder)
            legacy=run(a.base,'0',source,root);base=run(a.base,'require',source,root);actual=run(a.nift,'require',source,root);st=actual['stats'][-1] if actual['stats'] else {}
            refused=name in ['inline','factory'] and context in ['loop','loop-arg','fn-loop']
            if refused:
                ok=not actual['out'] and st.get('outcome')=='require-error' and st.get('vm_executions')==0 and st.get('legacy_executions')==0
                if a.vm_only_harness:
                    ok=ok and actual['exit']==2 and actual['err']=='not compiled: method:sort\n';auto=None
                else:
                    auto=run(a.nift,'1',source,root)
                    ok=ok and actual['exit']==1 and st.get('reason')=='method:sort' and all(legacy[k]==auto[k] for k in ['out','err','exit'])
            else:
                auto=None;ok=all(legacy[k]==actual[k] for k in ['out','err','exit']) and st.get('outcome')=='vm' and st.get('vm_executions')==1 and st.get('legacy_executions')==0
            rows.append(dict(kind=name,context=context,source=source,refused=refused,pass_=bool(ok),legacy=legacy,base=base,actual=actual,automatic=auto));print('PASS' if ok else 'FAIL',name,context)
    Path(a.output).write_text(json.dumps(rows,indent=2)+'\n');return 0 if all(r['pass_'] for r in rows) else 1
if __name__=='__main__':raise SystemExit(main())
