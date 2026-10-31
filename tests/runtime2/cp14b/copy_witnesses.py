#!/usr/bin/env python3
"""Exact D-040 witnesses: handwritten copy semantics, frozen legacy output.

Since D-NEXT-refsem (Phase A) the VM refuses them before any effect (the
forward-compatibility guard); the legacy oracle and the sources stay frozen.
"""
import argparse,hashlib,json,tempfile
from pathlib import Path
from functional import run

def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--base',required=True);p.add_argument('--output',required=True);a=p.parse_args();rows=[]
    fixtures=json.loads((Path(__file__).parent/'fixtures/copy-witnesses.json').read_text())
    with tempfile.TemporaryDirectory(prefix='cp14b-copy-witnesses-') as folder:
        for case in fixtures:
            root=Path(folder);legacy=run(a.base,'0',case['source'],root);actual=run(a.nift,'require',case['source'],root)
            st=actual['stats'][-1] if actual['stats'] else {};ok=hashlib.sha256(case['source'].encode()).hexdigest()==case['source_sha256']
            ok=ok and all(legacy[k]==case['legacy'][k] for k in ['out','err','exit'])
            # D-NEXT-refsem #1: every witness mutates an aliased array, whose output
            # changes once arrays are references (Phase B); the forward-compatibility
            # guard refuses it before any effect. `expected` stays the D-040 copy result.
            ok=ok and actual['out']=='' and actual['exit']==1 and st.get('outcome')=='require-error' and str(st.get('reason','')).startswith('reference-guard:') and st.get('vm_executions')==0 and st.get('legacy_executions')==0
            rows.append(dict(name=case['name'],source_sha256=case['source_sha256'],pass_=bool(ok),legacy=legacy,actual=actual));print('PASS' if ok else 'FAIL',case['name'])
    Path(a.output).write_text(json.dumps(dict(binary_sha256=hashlib.sha256(Path(a.nift).read_bytes()).hexdigest(),results=rows),indent=2)+'\n');return 0 if all(r['pass_'] for r in rows) else 1
if __name__=='__main__':raise SystemExit(main())
