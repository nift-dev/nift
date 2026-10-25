#!/usr/bin/env python3
"""The twelve approved design mutants, exact controls, and negative harness controls."""
import argparse,copy,hashlib,json,os,subprocess,tempfile
from pathlib import Path
from functional import CASES,run
from mutations import DRAIN
CONTROL='print([1,2].count(x=>x>0))'
TIES='print([{\"k\":0,\"id\":1},{\"k\":0,\"id\":2}].sort_by(x=>x.k).map(x=>x.id).stringify())'
DIRECTION='print([{\"k\":1,\"v\":2,\"id\":1},{\"k\":1,\"v\":1,\"id\":2},{\"k\":2,\"v\":1,\"id\":3}].sort_by(x=>x.k,"asc",x=>x.v,"desc").map(x=>x.id).stringify())'
BUDGET='fn(cb(x)) { if(x>0) { return [x-1].map(cb)[0] }; return 1 }; print([1].map(cb).stringify())'
SPLICE='a:=[1,2,3]; fn(repl()) { print(a.stringify()); return [9] }; a.splice(1,1,repl()); print(a.stringify())'
COPIES=[
'a:=[1,2]; a.pop(); print(a.stringify())',
'fn(f()) { a:=[1,2]; a.pop(); print(a.stringify()) }; f()',
'fn(f()) { a:=[1,2]; g:=()=>a.pop(); g(); print(a.stringify()) }; f()',
'a:=[[1,2]]; for(v:a) { v.pop() }; print(a.stringify())']
# name, translation unit, existing test macro (0 means source-copy injection), witnesses
MUTANTS=[
('ignored-callback-result','collection_helpers',0,[CASES['map'],'print([1,2,3].reduce((a,x)=>a+x,10))']),
('truthy-non-bool','collection_helpers',5,['print([1].filter(x=>1).stringify())','print([1].any(x=>1))','print([1].partition(x=>1).stringify())']),
('unstable-record-ties','collection_helpers',7,[TIES]),
('ignored-direction','compiler_collection',0,[DIRECTION]),
('copied-mutation-receiver','vm_array_mutation',0,COPIES),
('removed-const-guard','compiler_array_mutation',11,[CASES['const_precedence']]),
('callback-frame-budget-bypass','vm_collection',0,[BUDGET]),
('missing-callback-report-frame','vm_collection_report',0,[CASES['block_error']]),
('live-source-not-snapshot','compiler_collection',3,[CASES['snapshot_mutation'],'a:=[1,2,3]; fn(cb(x)) { a.pop(); a.push(9); return x }; print(a.map(cb).stringify()); print(a.stringify())','a:=[1,2,3]; fn(cb(x)) { a=[9,9,9]; return x }; print(a.map(cb).stringify()); print(a.stringify())']),
('selector-factory-hoisted','compiler_collection',0,[CASES['extra_factory'],CASES['sort_by_factories']]),
('splice-replacement-early','compiler_array_mutation',0,[SPLICE,CASES['splice_throw']]),
('premature-comparator-unwind','vm_collection_sort',9,[DRAIN+'; print(n)']),
]
def substitute(source,old,new):
    if source.count(old)!=1:raise RuntimeError('Injection anchor is not unique: '+old)
    return source.replace(old,new)
def inject(number,source):
    if number==1:
        source=substitute(source,'case M::Map: array_push(s[Result],r);break;','case M::Map: array_push(s[Result],s[Item]);break;')
        return substitute(source,'case M::Reduce:s[Result]=r;break;','case M::Reduce:s[Result]=s[Item];break;')
    if number==4:return substitute(source,'desc=string_view_of(decoded)=="desc";','desc=false;')
    if number==5:
        source=substitute(source,'Value* s=R+in.a;const auto m=static_cast<M>(in.b);','Value* s=R+in.a;const auto m=static_cast<M>(in.b); Value saved_receiver=s[Source];')
        return substitute(source,'heap_retally(s[Source].heap());return memory_ok(fault);','heap_retally(s[Source].heap());s[Source]=std::move(saved_receiver);return memory_ok(fault);')
    if number==7:return substitute(source,'frames_.size()>=next_frame_check_||callee_base+callee->nregs+1u>stack_.size()','callee_base+callee->nregs+1u>stack_.size()')
    if number==8:return substitute(source,'if((it->ret-1)->op==OpCode::CollectionCall&& !((it->ret-1)->c&kConstantBit))','if(false)')
    if number==10:return substitute(source,'const bool once=(method>=CollectionMethod::Map&&method<=CollectionMethod::Count)||method==CollectionMethod::GroupBy;','const bool once=true;')
    if number==11:
        source=substitute(source,'if(argc>1&&!expr_to(args[1],state+Returned))return false;','if(argc>1&&!expr_to(args[1],state+Returned))return false;\n    if(method==M::Splice&&argc==3&&!expr_to(args[2],state+Keys))return false;')
        return substitute(source,'if(!expr_to(args[2],state+Keys)||!read(true))return false;','if(!read(true))return false;')
    return source

def valid(row,marker):
    stats=row.get('stats')
    if not isinstance(stats,list) or len(stats)!=1:return False
    st=stats[0]
    if not isinstance(st,dict):return False
    return (st.get('marker')==marker and st.get('vm_executions')==1 and st.get('legacy_executions')==0
            and st.get('outcome') in ['vm','vm-error']
            and row.get('exit')==(0 if st['outcome']=='vm' else 1)
            and not any(s in row.get('err','') for s in ['AddressSanitizer','UndefinedBehaviorSanitizer','TIMEOUT']))
def verdict(number,normal,actual,marker):
    if not valid(normal,'control') or not valid(actual,marker):return 'HARNESS'
    if number==8:
        ok=(normal['stats'][0]['callback_frames']>0 and actual['stats'][0]['callback_frames']==0
            and all(normal[k]==actual[k] for k in ['out','err','exit']))
    elif number==7:
        ok=(normal['exit']==1 and normal['stats'][0]['reason']=='callable recursion depth exceeded: cb'
            and normal['stats'][0]['beyond_legacy_depth'] and actual['exit']==0
            and actual['out']=='[1]\n' and not actual['err'])
    else:
        # All intended defects produce a successful but semantically wrong
        # execution. Unrelated errors, crashes and fallback never detect one.
        ok=(actual['exit']==0 and not actual['err'] and
            any(normal[k]!=actual[k] for k in ['out','err','exit']))
    return 'DETECTED' if ok else 'SURVIVED'
def main():
    p=argparse.ArgumentParser();p.add_argument('--legacy',required=True);p.add_argument('--out',required=True);a=p.parse_args()
    out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
    intended_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
    driver_hash=hashlib.sha256(Path('tests/runtime2/cp14b/design_harness.cpp').read_bytes()).hexdigest()
    flags=['-Isrc','-Iinclude','-Itests/rt','-std=c++17','-O2','-Wall','-Wextra','-pedantic','-pthread']
    subprocess.run(['make','-j2','nift','.build/cp13b/nift-switch'],check=True)
    objects=[str(f) for root in ['src','minifypp','markuppp'] for f in Path(root).rglob('*.o') if str(f)!='src/nift.o']
    def build(name,stem=None,macro=0,number=0):
        driver=out/(name+'-driver.o');binary=out/name
        subprocess.run(['g++',*flags,'-DCP14B_MUTANT_MARKER="'+name+'"','-c','tests/runtime2/cp14b/design_harness.cpp','-o',str(driver)],check=True,capture_output=True)
        linked=objects
        source_hash=None
        if stem:
            source=Path('src/rt/'+stem+'.cpp').read_text();source=inject(number,source)
            copied=out/(name+'-'+stem+'.cpp');copied.write_text(source);obj=out/(name+'.o')
            command=['g++',*flags,*([f'-DNIFT_COLLECTION_MUTANT={macro}'] if macro else []),'-c',str(copied),'-o',str(obj)]
            result=subprocess.run(command,capture_output=True,text=True);(out/(name+'-build.log')).write_text(result.stdout+result.stderr)
            if result.returncode:raise RuntimeError(result.stderr)
            linked=[str(obj) if item=='src/rt/'+stem+'.o' else item for item in objects]
            source_hash=hashlib.sha256(copied.read_bytes()).hexdigest()
        subprocess.run(['g++','-pthread',str(driver),*linked,'-ldl','.build/libffi/x86_64-linux-gnu-cc/install/lib/libffi.a','-o',str(binary)],check=True,capture_output=True)
        return binary,dict(intended_commit=intended_commit,driver_sha256=driver_hash,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),source_sha256=source_hash,flags=flags,macro=macro,marker=name)
    normal,normal_identity=build('control');results=[];negatives=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-design-mutants-') as folder:
        root=Path(folder)
        for number,(name,stem,macro,witnesses) in enumerate(MUTANTS,1):
            binary,identity=build(name,stem,macro,number);records=[]
            for source in witnesses:
                if number==7:os.environ['NIFT_CP14B_TEST_MAX_FRAMES']='1'
                else:os.environ.pop('NIFT_CP14B_TEST_MAX_FRAMES',None)
                if hashlib.sha256(normal.read_bytes()).hexdigest()!=normal_identity['binary_sha256'] or hashlib.sha256(binary.read_bytes()).hexdigest()!=identity['binary_sha256']:
                    raise RuntimeError('Stale executable identity')
                control=run(str(normal),'require',source,root)
                if not valid(control,'control'):raise RuntimeError('Invalid normal execution: '+name+' '+str(control))
                oracle=run(a.legacy,'0',source,root) if number!=7 else None
                if oracle and any(control[k]!=oracle[k] for k in ['out','err','exit']):raise RuntimeError('Normal/legacy semantic mismatch: '+name+' '+str(control)+' '+str(oracle))
                actual=run(str(binary),'require',source,root);status=verdict(number,control,actual,name)
                records.append(dict(source=source,status=status,normal=control,oracle=oracle,mutant=actual))
            os.environ.pop('NIFT_CP14B_TEST_MAX_FRAMES',None)
            baseline_control=run(str(normal),'require',CONTROL,root);mutant_control=run(str(binary),'require',CONTROL,root)
            unaffected=valid(mutant_control,name) and all(mutant_control[k]==baseline_control[k] for k in ['out','err','exit'])
            passed=unaffected and all(row['status']=='DETECTED' for row in records)
            results.append(dict(number=number,name=name,detected=passed,identity=identity,witnesses=records,unaffected_control=mutant_control))
            print('DETECTED' if passed else 'FAIL',number,name,flush=True)
            sample=records[0]
            for label in ['missing-stats','malformed-stats','duplicate-stats','fallback','stale-normal','post-statistics-crash','unrelated-error','timeout','wrong-marker']:
                bad=copy.deepcopy(sample['mutant']);old=copy.deepcopy(sample['normal'])
                if label=='missing-stats':bad['stats']=[]
                elif label=='malformed-stats':bad['stats']=[None]
                elif label=='duplicate-stats':bad['stats']*=2
                elif label=='fallback':bad['stats'][0]['legacy_executions']=1
                elif label=='stale-normal':old['stats'][0]['marker']='stale-control'
                elif label=='post-statistics-crash':bad['exit']=-11
                elif label=='unrelated-error':bad['exit']=70;bad['err']='unrelated failure'
                elif label=='timeout':bad['exit']=124;bad['err']='TIMEOUT'
                else:bad['stats'][0]['marker']='wrong'
                ok=verdict(number,old,bad,name)=='HARNESS';negatives.append(dict(mutant=name,case=label,pass_=ok))
            (out/'results.json').write_text(json.dumps(dict(results=results,negative_controls=negatives,normal_identity=normal_identity),indent=2)+'\n')
    raise SystemExit(0 if all(r['detected'] for r in results) and all(r['pass_'] for r in negatives) else 1)
if __name__=='__main__':main()
