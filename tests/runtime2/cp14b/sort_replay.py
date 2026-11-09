#!/usr/bin/env python3
"""Production VM gate against the D4 cases, no new pre-controller runs.

B04 (owner approval 2026-11-09 17:31; owner ruling Q7, 2026-11-07, copy-sort-commit): the expected
results are fixtures/d4-q7-expectations.json.gz, derived mechanically from the frozen D4 traces
(--oracle, unchanged historical evidence of the superseded in-place semantics) by
q7_expectations.py, and checked against them and their pinned sha256s before any case runs. Each
case's actual trace (every call's pair and receiver view, in order), final array, call count and
failure flag must equal the derived expectation exactly; the pairs, count and flag are the frozen
trace's own (re-asserted here per case)."""
import argparse, gzip, hashlib, json, os, subprocess, tempfile
from pathlib import Path
from d4_validate import nift_source, limits
import q7_expectations as q7

def main():
    p=argparse.ArgumentParser();p.add_argument('--sanitizer',action='store_true');p.add_argument('--nift',required=True);p.add_argument('--oracle',required=True);p.add_argument('--output',required=True);p.add_argument('--legacy',help='CLI legacy oracle (required for a VM-only harness on a refused profile)');p.add_argument('--expected',default=str(q7.DERIVED),help='the derived Q7 expectations (B04)');a=p.parse_args()
    raw=Path(a.oracle).read_bytes();oracle=json.loads(gzip.decompress(raw) if a.oracle.endswith('.gz') else raw);results=[]
    # B04: the derived expectations are exactly what the generator makes from the pinned frozen traces
    problems=q7.check_derived(a.expected,a.oracle)
    for problem in problems:print('FAIL q7 expectations:',problem)
    if problems:return 1
    expected={c['case']:c['expected'] for c in q7.load(a.expected)['cases']}
    for record in oracle['results']:
        e=expected.get(record['case']['id']);n=record['native']
        if e is None or [(c['l'],c['r']) for c in e['trace']]!=[(c['l'],c['r']) for c in n['trace']] or e['calls']!=n['calls'] or e['failed']!=n['failed']:
            print('FAIL q7 expectations: case',record['case']['id'],'does not keep the frozen pairs, count and failure flag');return 1
    if len(expected)!=len(oracle['results']):print('FAIL q7 expectations: case sets differ');return 1
    import comparator_profile as cprof
    prof=cprof.profile(a.nift);unverified=0
    with tempfile.TemporaryDirectory(prefix='cp14b-sort-replay-') as folder:
        root=Path(folder)
        for record in oracle['results']:
            case=record['case'];source=nift_source(case)
            if prof.refused:
                # Legacy here is this binary's own NIFT_VM=0 engine (the CLI); the harness has none.
                ok,detail=cprof.refused_ok(prof,a.nift,a.legacy or a.nift,source,root);unverified+=1
                results.append(dict(case=case['id'],pass_=bool(ok),coverage='unverified-comparator',detail=detail))
                if not ok:print('FAIL',case['id'],'expected the exact method:sort refusal and legacy parity')
                continue
            (root/'p.f').write_text(source)
            stats=root/'stats';stats.unlink(missing_ok=True)
            proc=subprocess.run([a.nift,str(root/'p.f')],env=dict(os.environ,NIFT_VM='require',NIFT_VM_STATS=str(stats),NIFT_PREVALIDATE='off'),capture_output=True,text=True,timeout=5,preexec_fn=None if a.sanitizer else limits)
            trace=[];final=None;count=None;failed=False;unexpected=[]
            for line in proc.stdout.splitlines():
                if line.startswith('T'):
                    l,r,array=map(json.loads,line[1:].split('|'));trace.append(dict(l=l,r=r,a=array))
                elif line.startswith('A'):final=json.loads(line[1:])
                elif line.startswith('N'):count=int(line[1:])
                elif line=='Estop':failed=True
                else:unexpected.append(line)
            got=dict(trace=trace,final=final,calls=count,failed=failed)
            st=[json.loads(line) for line in stats.read_text().splitlines()] if stats.exists() else []
            ok=proc.returncode==0 and not unexpected and got==expected[case['id']] and st and st[-1]['outcome']=='vm' and st[-1]['legacy_executions']==0
            results.append(dict(case=case['id'],pass_=bool(ok),actual=got,expected=expected[case['id']],exit=proc.returncode,stderr=proc.stderr,stats=st))
            if not ok:print('FAIL',case['id'],proc.returncode,proc.stderr[:160])
    if prof.refused:print(cprof.banner(prof,unverified,'sort replay'))
    report=dict(status='PASS' if all(r['pass_'] for r in results) else 'FAIL',comparator_profile=prof.found,unverified_comparator_cases=unverified,binary_sha256=hashlib.sha256(Path(a.nift).read_bytes()).hexdigest(),oracle_sha256=hashlib.sha256(Path(a.oracle).read_bytes()).hexdigest(),expected_sha256=hashlib.sha256(Path(a.expected).read_bytes()).hexdigest(),expectation='Q7 copy-sort-commit (q7_expectations.py)',results=results)
    Path(a.output).write_text(json.dumps(report,indent=2)+'\n');print((report['status']+' '+str(len(results))+' production VM traces') if not prof.refused else (('UNVERIFIED' if report['status']=='PASS' else 'FAIL')+' '+str(len(results))+' D4 cases refused with method:sort (no VM trace replayed)'));return 0 if report['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
