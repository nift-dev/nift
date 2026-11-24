#!/usr/bin/env python3
"""Build real named mutants, require passing controls, then semantic detection."""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
from functional import APPROVED,CASES,run
from build_drain import check_drain
import sys as _sys,os as _os
_sys.path.insert(0,_os.path.join(_os.path.dirname(_os.path.abspath(__file__)),'..'))
import build_toolchain as toolchain
DRAIN='a:=[{"k":3},{"k":1},{"k":2}]; n:=0; fn(cmp(x,y)) { n+=1; if(n==2) { throw error("stop") }; return x.k<y.k }; try { a.sort(cmp) } catch(e) { print("CATCH"); print(a.stringify()) }; print("AFTER")'
# B04 (owner approval 2026-11-09 17:31): the DRAIN control's VM result is the copy-sort-commit one
# (owner ruling Q7, 2026-11-07, docs/runtime-2/compatibility.md: a failed comparator sort leaves the
# receiver unchanged), an approved difference from legacy, which leaves the permutation of its
# in-place sort. Both sides are pinned exactly: the VM's Q7 output, and legacy's own output.
APPROVED_CONTROLS={DRAIN:dict(vm=dict(out='CATCH\n[{"k":3},{"k":1},{"k":2}]\nAFTER\n',err='',exit=0),
                              legacy=dict(out='CATCH\n[{"k":1},{"k":3},{"k":2}]\nAFTER\n',err='',exit=0),
                              citation='owner ruling Q7 (2026-11-07), copy-sort-commit; B04 approval 2026-11-09 17:31')}
# C47: a witness the functional suite already pins as an approved difference (functional.APPROVED)
# is pinned the same way here: the VM to the approved output, legacy to its own recorded output.
# const_precedence (owner Q2 option B, `const` is binding-only, B5) stays pinned here, run as an
# approved control on its own (below) since its mutant, 11, is retired.
for _name,_want in APPROVED.items():
    if 'vm_err' in _want:
        APPROVED_CONTROLS.setdefault(CASES[_name],dict(vm=dict(out=_want['vm'],err=_want['vm_err'],exit=_want['vm_exit']),
            legacy=dict(out=_want['legacy'],err=_want['legacy_err'],exit=_want['legacy_exit']),citation=_want['citation']))
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
]
# Mutant 11 (missing-const-guard, compiler_array_mutation, witness const_precedence) is RETIRED as
# obsolete (combined-b3-b5.md sections 40 and 44; review of C47-C49): B5 2 (519b9690) deleted its
# hook with the const-array guard it removed, and owner Q2 option B (`const` is binding-only) makes
# that guard obsolete. Built with this file's own command, -DNIFT_COLLECTION_MUTANT=11 gives the same
# preprocessed source and a byte-identical object. The const contract's coverage is listed in
# section 44 (const_admission.py, cp25's const differential, functional.py's exact pins).
RETIRED={11:('missing-const-guard','compiler_array_mutation','const_precedence')}
def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--out',required=True);a=p.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    objects=toolchain.cli_objects()  # B10: nift's own object list, not rglob('*.o')
    results=[]
    import comparator_profile as cprof
    prof=cprof.profile(a.nift);unverified=0
    with tempfile.TemporaryDirectory(prefix='cp14b-mutants-') as folder:
        root=Path(folder)
        mutated={m[3] for m in MUTANTS}
        for source,want in APPROVED_CONTROLS.items():
            if source in mutated:continue  # checked as that mutant's control below
            label=next((n for n,c in CASES.items() if c==source),source[:40])
            normal=run(a.nift,'require',source,root);oracle=run(a.nift,'0',source,root)
            st=normal['stats'][-1] if normal['stats'] else {}
            if any(normal[k]!=want['vm'][k] for k in ['out','err','exit']):
                raise RuntimeError('Approved control failed its VM pin: '+label)
            if any(oracle[k]!=want['legacy'][k] for k in ['out','err','exit']):
                raise RuntimeError('Approved control: legacy differs from its recorded output: '+label)
            if st.get('outcome') not in ['vm','vm-error'] or st.get('vm_executions')!=1 or st.get('legacy_executions')!=0:
                raise RuntimeError('Approved control did not execute on the VM: '+label)
            print('PASS approved control',label,'('+want['citation']+')',flush=True)
        for number,name,stem,source in MUTANTS:
            if prof.refused and cprof.is_comparator(source):
                # The production gate refuses this witness before the mutated code can run.
                ok,detail=cprof.refused_ok(prof,a.nift,a.nift,source,root);unverified+=1
                results.append(dict(id=number,name=name,source=source,detected=bool(ok),coverage='unverified-comparator',detail=detail))
                print('UNVERIFIED' if ok else 'FAIL',number,name,flush=True);continue
            normal=run(a.nift,'require',source,root)
            oracle=run(a.nift,'0',source,root)
            if source in APPROVED_CONTROLS:
                want=APPROVED_CONTROLS[source]
                if any(normal[k]!=want['vm'][k] for k in ['out','err','exit']):
                    raise RuntimeError('Normal control failed the approved Q7 semantics: '+name)
                if any(oracle[k]!=want['legacy'][k] for k in ['out','err','exit']):
                    raise RuntimeError('Legacy control differs from its recorded output (Q7 difference): '+name)
            elif any(normal[k]!=oracle[k] for k in ['out','err','exit']):
                raise RuntimeError('Normal control failed exact legacy semantics: '+name)
            st=normal['stats'][-1] if normal['stats'] else {}
            if st.get('outcome') not in ['vm','vm-error'] or st.get('vm_executions')!=1 or st.get('legacy_executions')!=0:
                raise RuntimeError('Normal control did not execute: '+name)
            obj=out/f'm{number}.o';binary=out/f'm{number}'
            # B10: nift's own compile line (compiler, production guard, CPPFLAGS, CXXFLAGS) plus the
            # mutant define, into this non-production directory; the object must differ from nift's.
            toolchain.run(toolchain.production_compile(f'-DNIFT_COLLECTION_MUTANT={number}','-c',f'src/rt/{stem}.cpp','-o',str(obj)),
                          'mutant compile '+name,log=out/f'm{number}-build.log')
            if obj.read_bytes()==Path(f'src/rt/{stem}.o').read_bytes():
                raise RuntimeError(f'Mutant {number} {name} compiled to the production object src/rt/{stem}.o')
            linked=[str(obj) if o==f'src/rt/{stem}.o' else o for o in objects]
            if linked==objects:raise RuntimeError(f'Mutant {number} {name}: src/rt/{stem}.o is not in nift\'s object list')
            toolchain.run(toolchain.link(linked,binary),'mutant link '+name)
            actual=run(str(binary),'require',source,root);actual_st=actual['stats'][-1] if actual['stats'] else {}
            if number in [1,2]:
                reason='collection boundary drain failed' if number==1 else 'out of memory (std::bad_alloc)'
                detected=actual['exit']==70 and actual_st.get('outcome')=='internal-error' and actual_st.get('reason')==reason and actual_st.get('vm_executions')==1 and actual_st.get('legacy_executions')==0 and 'CATCH' not in actual['out'] and 'AFTER' not in actual['out']
            else:
                detected=actual['exit'] in [0,1] and actual_st.get('vm_executions')==1 and actual_st.get('legacy_executions')==0 and any(actual[k]!=normal[k] for k in ['out','err','exit'])
            evidence = dict(id=number, name=name, source=source, normal=normal, oracle=oracle,
                            mutant=actual, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
            if (number == 9 and not detected and actual_st.get('outcome') == 'vm' and
                actual_st.get('vm_executions') == 1 and actual_st.get('legacy_executions') == 0 and
                all(actual[k] == normal[k] for k in ['out', 'err', 'exit'])):
                # Q7 keeps the receiver intact even when the drain is
                # skipped. Check controller progress in the allocation test.
                observed = check_drain(out / 'skip-drain-control', detect_skip=True)
                detected = observed['detected']
                evidence['drain_observer'] = observed
            results.append(dict(evidence, detected=bool(detected)))
            print('DETECTED' if detected else 'SURVIVED',number,name,flush=True)
    print(cprof.banner(prof,unverified,'named mutants'))
    (out/'results.json').write_text(json.dumps(dict(comparator_profile=prof.found,unverified_comparator_mutants=unverified,results=results),indent=2)+'\n');return 0 if all(r['detected'] for r in results) else 1
if __name__=='__main__':raise SystemExit(main())
