#!/usr/bin/env python3
"""Exact D-040 witnesses: handwritten copy semantics, frozen legacy output.

Since D-NEXT-refsem (Phase A) the VM refused them before any effect (the
forward-compatibility guard); the legacy oracle and the sources stay frozen.
B5 (b5-refsem.md section 6, class i): the guard is deleted and every witness runs.
Its result is legacy's except the lines B5_DIFF pins (plan section 3 "Call arguments",
Q3: the callee's push reaches the caller's array; legacy copies arguments).
"""
import argparse,hashlib,json,tempfile
from pathlib import Path
from functional import run
B5_DIFF={'generated-102':('[1,2]\n','[1,2,99]\n'),'generated-162':('[1,2]\n','[1,2,99]\n')}
def b5_expected(case):
    out=case['legacy']['out']
    if case['name'] in B5_DIFF:
        old,new=B5_DIFF[case['name']]
        assert out.count(old)==1
        out=out.replace(old,new)
    return out

def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--base',required=True);p.add_argument('--output',required=True);a=p.parse_args();rows=[]
    fixtures=json.loads((Path(__file__).parent/'fixtures/copy-witnesses.json').read_text())
    with tempfile.TemporaryDirectory(prefix='cp14b-copy-witnesses-') as folder:
        for case in fixtures:
            root=Path(folder);legacy=run(a.base,'0',case['source'],root);actual=run(a.nift,'require',case['source'],root)
            st=actual['stats'][-1] if actual['stats'] else {};ok=hashlib.sha256(case['source'].encode()).hexdigest()==case['source_sha256']
            ok=ok and all(legacy[k]==case['legacy'][k] for k in ['out','err','exit'])
            # B5: every witness runs on the VM with legacy's result, except B5_DIFF's lines.
            ok=ok and actual['out']==b5_expected(case) and actual['exit']==case['legacy']['exit'] and actual['err']==case['legacy']['err'] and st.get('outcome')=='vm' and st.get('vm_executions')==1 and st.get('legacy_executions')==0
            rows.append(dict(name=case['name'],source_sha256=case['source_sha256'],pass_=bool(ok),legacy=legacy,actual=actual));print('PASS' if ok else 'FAIL',case['name'])
    Path(a.output).write_text(json.dumps(dict(binary_sha256=hashlib.sha256(Path(a.nift).read_bytes()).hexdigest(),results=rows),indent=2)+'\n');return 0 if all(r['pass_'] for r in rows) else 1
if __name__=='__main__':raise SystemExit(main())
