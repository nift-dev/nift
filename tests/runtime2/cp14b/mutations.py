#!/usr/bin/env python3
"""Build real named mutants, require passing controls, then semantic detection."""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
from functional import CASES,run
DRAIN='a:=[{"k":3},{"k":1},{"k":2}]; n:=0; fn(cmp(x,y)) { n+=1; if(n==2) { throw error("stop") }; return x.k<y.k }; try { a.sort(cmp) } catch(e) { print("CATCH"); print(a.stringify()) }; print("AFTER")'
MUTANTS=[
(1,'logical-drain-failure','vm_collection_sort',DRAIN),
(2,'escaping-drain-bad-alloc','vm_collection_sort',DRAIN),
(3,'live-source-instead-of-snapshot','compiler_collection',CASES['snapshot_mutation']),
(4,'core-factory-reevaluated','compiler_collection',CASES['core_factory']),
(5,'truthy-predicate','collection_helpers',CASES['predicate_error']),
(6,'no-short-circuit','collection_helpers',CASES['short_any']),
(7,'unstable-sort-by-ties','collection_helpers',CASES['sort_by_stable']),
(8,'group-each-duplicate-membership','collection_helpers',CASES['group_by_each']),
(9,'skip-caught-sort-drain','vm_collection_sort',DRAIN),
(10,'duplicate-callback-prefix','vm_collection_report',CASES['block_error']),
(11,'missing-const-guard','compiler_array_mutation',CASES['const_precedence']),
]
def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--out',required=True);a=p.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    objects=[str(p) for root in ['src','minifypp','markuppp'] for p in Path(root).rglob('*.o')]
    results=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-mutants-') as folder:
        root=Path(folder)
        for number,name,stem,source in MUTANTS:
            normal=run(a.nift,'require',source,root)
            oracle=run(a.nift,'0',source,root)
            if any(normal[k]!=oracle[k] for k in ['out','err','exit']):
                raise RuntimeError('Normal control failed exact legacy semantics: '+name)
            st=normal['stats'][-1] if normal['stats'] else {}
            if st.get('outcome') not in ['vm','vm-error'] or st.get('vm_executions')!=1 or st.get('legacy_executions')!=0:
                raise RuntimeError('Normal control did not execute: '+name)
            obj=out/f'm{number}.o';binary=out/f'm{number}'
            proc=subprocess.run(['g++','-Isrc','-Iinclude','-std=c++17','-O2','-pthread',f'-DNIFT_COLLECTION_MUTANT={number}','-c',f'src/rt/{stem}.cpp','-o',str(obj)],capture_output=True,text=True)
            (out/f'm{number}-build.log').write_text(proc.stdout+proc.stderr)
            if proc.returncode:raise RuntimeError('Mutant compile failure: '+name+' '+proc.stderr)
            linked=[str(obj) if o==f'src/rt/{stem}.o' else o for o in objects]
            subprocess.run(['g++','-pthread',*linked,'-ldl','.build/libffi/x86_64-linux-gnu-cc/install/lib/libffi.a','-o',str(binary)],check=True,capture_output=True)
            actual=run(str(binary),'require',source,root);actual_st=actual['stats'][-1] if actual['stats'] else {}
            if number in [1,2]:
                reason='collection boundary drain failed' if number==1 else 'out of memory (std::bad_alloc)'
                detected=actual['exit']==70 and actual_st.get('outcome')=='internal-error' and actual_st.get('reason')==reason and actual_st.get('vm_executions')==1 and actual_st.get('legacy_executions')==0 and 'CATCH' not in actual['out'] and 'AFTER' not in actual['out']
            else:
                detected=actual['exit'] in [0,1] and actual_st.get('vm_executions')==1 and actual_st.get('legacy_executions')==0 and any(actual[k]!=normal[k] for k in ['out','err','exit'])
            results.append(dict(id=number,name=name,source=source,detected=bool(detected),normal=normal,oracle=oracle,mutant=actual,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
            print('DETECTED' if detected else 'SURVIVED',number,name,flush=True)
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n');return 0 if all(r['detected'] for r in results) else 1
if __name__=='__main__':raise SystemExit(main())
