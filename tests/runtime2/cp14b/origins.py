#!/usr/bin/env python3
"""Owner-approved actual Error origins; all other observable fields stay frozen."""
import argparse,hashlib,json,re,tempfile
from pathlib import Path
from functional import run

def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--base',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    fixtures=json.loads((Path(__file__).parent/'fixtures/origins.json').read_text());rows=[]
    import comparator_profile as cprof
    prof=cprof.profile(a.nift);unverified=0
    with tempfile.TemporaryDirectory(prefix='cp14b-origin-') as tmp:
        for case in fixtures['cases']:
            if prof.refused and cprof.is_comparator(case['source']):
                ok,detail=cprof.refused_ok(prof,a.nift,a.base,case['source'],Path(tmp));unverified+=1
                rows.append(dict(name=case['name'],pass_=bool(ok),coverage='unverified-comparator',detail=detail));print('UNVERIFIED' if ok else 'FAIL',case['name']);continue
            root=Path(tmp);actual=run(a.nift,'require',case['source'],root);legacy=run(a.base,'0',case['source'],root)
            observable=lambda r:{k:r[k] for k in ['out','err','exit']}
            stats=actual['stats'];ok=observable(actual)==case['expected'] and observable(legacy)==case['legacy']
            ok=bool(ok and len(stats)==1 and stats[0]['vm_executions']==1 and stats[0]['legacy_executions']==0 and stats[0]['outcome'] in ['vm','vm-error'])
            # Preserve the approved distinction explicitly; no broad policy waiver.
            if not case['approved_origin_difference']:ok=ok and observable(actual)==observable(legacy)
            for span in case['throw_spans']:
                ok=ok and case['source'][span['begin']:span['end']]==span['text']
                ok=ok and span['line']==case['source'].count('\n',0,span['begin'])+1
                ok=ok and span['column']==span['begin']-case['source'].rfind('\n',0,span['begin'])
            # Independently pin each golden origin to an actual Throw AST span.
            text=case['expected']['out']
            found=re.search(r'stop\n(\d+)\n(\d+)\n',text)
            if not found and 'cause' in case['name']:
                found=re.search(r'(\d+)\n(\d+)\n$',text)
            if not found and case['expected']['err']:
                found=re.search(r'case.f:(\d+):(\d+):',case['expected']['err'])
            ok=bool(ok and found and tuple(map(int,found.groups())) in
                    [(span['line'],span['column']) for span in case['throw_spans']])
            rows.append(dict(name=case['name'],pass_=bool(ok),approved_origin_difference=case['approved_origin_difference'],actual=actual,legacy=legacy));print('PASS' if ok else 'FAIL',case['name'])
    print(cprof.banner(prof,unverified,'origins'))
    result=dict(status='PASS' if all(r['pass_'] for r in rows) else 'FAIL',comparator_profile=prof.found,unverified_comparator_cases=unverified,binary_sha256=hashlib.sha256(Path(a.nift).read_bytes()).hexdigest(),results=rows)
    Path(a.output).write_text(json.dumps(result,indent=2)+'\n');return 0 if result['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
