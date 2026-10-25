#!/usr/bin/env python3
"""Ordinary reporting is compared to accepted CP13B, collection reporting to legacy."""
import argparse,hashlib,json,tempfile
from pathlib import Path
from functional import run,CASES
ORDINARY=[
'fn(f()) { return 1/0 }; f()',
'print(1/0)',
'g:=x=>x/0; g(1)',
'fn(f()) { throw error("ordinary") }; f()',
'e:=error("ordinary"); try { throw e } catch(c) { print(c==e); print(c.message); print(c.code) }',
'fn(f()) { try { throw error("ordinary") } catch(e) { throw e } }; f()',
]
def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--base',required=True);p.add_argument('--output',required=True);a=p.parse_args();rows=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-reporting-') as folder:
        root=Path(folder)
        for source in ORDINARY:
            baseline=run(a.base,'require',source,root);actual=run(a.nift,'require',source,root)
            ok=all(baseline[k]==actual[k] for k in ['out','err','exit']) and actual['stats'] and actual['stats'][-1]['vm_executions']==1 and actual['stats'][-1]['legacy_executions']==0
            rows.append(dict(kind='ordinary accepted CP13B',source=source,pass_=bool(ok),baseline=baseline,actual=actual))
        for name in ['return_comparator_result_error','return_collection_predicate_error','return_mutator_error','return_callback_arity_error','return_method_arity_error']:
            source=CASES[name];baseline=run(a.base,'0',source,root);actual=run(a.nift,'require',source,root)
            ok=all(baseline[k]==actual[k] for k in ['out','err','exit']) and actual['stats'] and actual['stats'][-1]['vm_executions']==1 and actual['stats'][-1]['legacy_executions']==0
            rows.append(dict(kind=name,source=source,pass_=bool(ok),baseline=baseline,actual=actual))
    report=dict(status='PASS' if all(r['pass_'] for r in rows) else 'FAIL',binary_sha256=hashlib.sha256(Path(a.nift).read_bytes()).hexdigest(),results=rows)
    Path(a.output).write_text(json.dumps(report,indent=2)+'\n');print(report['status'],len(rows),'reporting contracts');return 0 if report['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
