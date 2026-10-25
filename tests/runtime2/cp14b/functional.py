#!/usr/bin/env python3
"""CP14B source parity witnesses; require proves no fallback and no replay."""
import argparse,json,os,re,subprocess,tempfile
from pathlib import Path
CASES={
'map':'print([1,2,3].map(x=>x*2).stringify())',
'filter':'print([1,2,3].filter(x=>x>1).stringify())',
'reduce':'print([1,2,3].reduce((a,x)=>a+x,0))',
'any':'print([1,2,3].any(x=>x>1))',
'all':'print([1,2,3].all(x=>x>1))',
'find':'print([1,2,3].find(x=>x>1))',
'find_index':'print([1,2,3].find_index(x=>x>1))',
'count':'print([1,2,3].count(x=>x>1))',
'sort_by':'print([3,1,2].sort_by(x=>x).stringify())',
'group_by':'g := [1,2,3].group_by(x=>x%2); print(g["1"].join(","))',
'index_by':'g := [1,2,3].index_by(x=>x); print(g["2"]+1)',
'partition':'g := [1,2,3].partition(x=>x>1); print(g.matched.join(","))',
'unique_by':'print([1,2,1].unique_by(x=>x).stringify())',
'min_by':'print([1,2,3].min_by(x=>0-x))',
'max_by':'print([1,2,3].max_by(x=>0-x))',
'count_by':'g := [1,2,3].count_by(x=>x%2); print(g["1"]+1)',
'group_by_each':'g := [1,2,3].group_by_each(x=>[x%2,x%2]); print(g["1"].join(","))',
'push':'a := [1,2,3]; print(a.push(4)); print(a.stringify())',
'pop':'a := [1,2,3]; print(a.pop()); print(a.stringify())',
'insert':'a := [1,2,3]; print(a.insert(1,4)); print(a.stringify())',
'remove':'a := [1,2,3]; print(a.remove(1)); print(a.stringify())',
'clear':'a := [1,2,3]; print(a.clear()); print(a.stringify())',
'splice':'a := [1,2,3]; print(a.splice(1,1,[4,5]).stringify()); print(a.stringify())',
'reverse':'a := [1,2,3]; print(a.reverse().stringify()); print(a.stringify())',
'sort':'a := [3,1,2]; print(a.sort().stringify()); print(a.stringify())',
'comparator':'a := [3,1,2]; print(a.sort((x,y)=>x<y).stringify()); print(a.stringify())',
'captured':'fn(f()) { a := [1,2]; g := () => a.pop(); print(g()); print(a.stringify()) }; f()',
'empty_map':'print([].map(x=>x*2).stringify())',
'empty_bad_core':'print([].map(1).stringify())',
'empty_bad_extra':'print([].index_by(1).stringify())',
'empty_min':'print([].min_by(1))',
'predicate_error':'print([1].filter(x=>x).stringify())',
'arity':'print([1].map((x,y)=>x).stringify())',
'group_key_error':'print([1].group_by(x=>[x]).stringify())',
'index_duplicate':'print([1,1].index_by(x=>x).stringify())',
'group_each_error':'print([1].group_by_each(x=>x).stringify())',
'unique_structural':'print([1,2,3,4].unique_by(x=>[x%2]).stringify())',
'sort_by_direction':'print([1,3,2].sort_by(x=>x,"desc").stringify())',
'block_assignment':'n := 0; print([1,2].map(x=>{ n += x }).stringify()); print(n)',
'block_error':'n := 0; print([1].map(x=>{ n += x/0 }).stringify())',
'block_return':'print([1].map(x=>{ return x+1 }).stringify())',
'explicit_error':'fn(boom(x)) { throw error("boom") }; try { [1].map(boom) } catch(e) { print(e.message); print(e.line); print(e.column) }',
'splice_throw':'a := [1,2,3]; fn(boom()) { throw error("boom") }; try { a.splice(1,1,boom()) } catch(e) { print(e.message); print(a.stringify()) }',
}
CASES.update({
'ordinary_block_error':'n:=0; f:=x=>{ n+=x/0 }; f(1)',
'nested_callback_error':'print([1].map(x=>[x].map(y=>y/0)[0]).stringify())',
'comparator_uncaught':'a:=[3,2,1]; fn(cmp(x,y)) { throw error("stop") }; a.sort(cmp)',
'comparator_internal_catch':'a:=[3,2,1]; fn(cmp(x,y)) { try { throw error("inside") } catch(e) { }; return x<y }; a.sort(cmp); print(a.stringify())',
'callback_rethrow':'fn(cb(x)) { try { throw error("stop") } catch(e) { throw e } }; try { [1].map(cb) } catch(e) { print(e.message); print(e.code); print(e.line); print(e.column) }',
'callback_rethrow_uncaught':'fn(cb(x)) { try { throw error("stop") } catch(e) { throw e } }; [1].map(cb)',
'nested_sort_internal':'a:=[3,2,1]; b:=[6,5,4]; fn(inside(x,y)) { throw error("inner") }; fn(outer(x,y)) { try { b.sort(inside) } catch(e) { print(b.stringify()) }; return x<y }; a.sort(outer); print(a.stringify())',
'nested_sort_escape':'a:=[3,2,1]; b:=[6,5,4]; fn(inside(x,y)) { throw error("inner") }; fn(outer(x,y)) { b.sort(inside); return x<y }; try { a.sort(outer) } catch(e) { print(e.message); print(b.stringify()); print(a.stringify()) }',
})

CASES.update({
'snapshot_mutation':'a:=[1,2,3]; fn(cb(x)) { a[1]=99; return x }; print(a.map(cb).stringify()); print(a.stringify())',
'snapshot_growth':'a:=[1,2,3]; fn(cb(x)) { a.push(9); return x }; print(a.map(cb).stringify()); print(a.stringify())',
'snapshot_rebind':'a:=[1,2,3]; fn(cb(x)) { a=[9]; return x }; print(a.map(cb).stringify()); print(a.stringify())',
'core_factory':'n:=0; fn(mk()) { n+=1; return x=>x }; print([1,2,3].map(mk()).stringify()); print(n)',
'extra_factory':'n:=0; fn(mk()) { n+=1; return x=>x }; print([1,2,3].index_by(mk()).stringify()); print(n)',
'sort_by_factories':'n:=0; fn(mk()) { n+=1; return x=>x }; print([3,1,2].sort_by(mk(),"asc",mk(),"desc").stringify()); print(n)',
'short_any':'n:=0; fn(cb(x)) { n+=1; return x>1 }; print([1,2,3].any(cb)); print(n)',
'short_all':'n:=0; fn(cb(x)) { n+=1; return x<2 }; print([1,2,3].all(cb)); print(n)',
'const_precedence':'const a:=[1]; fn(arg()) { print("ARG"); return 9 }; print(a.push(arg()))',
'const_pop_empty':'const a:=[]; a.pop()',
'const_capture':'fn(f()) { const a:=[1]; g:=()=>a.pop(); g() }; f()',
'sort_by_stable':'print([3,1,2].sort_by(x=>0).stringify())',
'plain_single_bool':'a:=[true]; a.sort(); print(a.stringify())',
'plain_bad_bool':'a:=[true,false]; a.sort()',
'comparator_singleton':'a:=[1]; a.sort(1); print(a.stringify())',
'comparator_bad_result':'a:=[2,1]; a.sort((x,y)=>x+y)',
'comparator_factory':'a:=[3,2,1]; n:=0; fn(mk()) { n+=1; return (x,y)=>x<y }; a.sort(mk()); print(a.stringify()); print(n)',
'nested_named_failure':'fn(g(y)) { return y/0 }; print([1].map(x=>[x].map(g)[0]).stringify())',
'nested_return_failure':'fn(f(x)) { return [x].map(y=>y/0)[0] }; print([1].map(f).stringify())',
})

# Wrong arities are fixed legacy guards, with no argument evaluation.
for method,args in {
'push':'','pop':'1','insert':'1','remove':'','clear':'1','splice':'','reverse':'1','sort':'1,2',
'map':'','filter':'','reduce':'x=>x','any':'','all':'','find':'','find_index':'','count':'',
'sort_by':'','group_by':'','index_by':'','partition':'','unique_by':'','min_by':'','max_by':'','count_by':'','group_by_each':''}.items():
    CASES['arity_guard_'+method]='a:=[1,2]; r:=a.'+method+'('+args+')'
CASES.update({
'direction_variable_guard':'d:="asc"; a:=[2,1]; r:=a.sort_by(x=>x,d)',
'direction_bad_guard':'a:=[2,1]; r:=a.sort_by(x=>x,"up")',
'escaped_direction':'print([2,1].sort_by(x=>x,"\\u0061sc").stringify())',
'iterator_mutation':'a:=[[1,2],[3]]; for(v:a) { v.push(9) }; print(a.stringify())',
'captured_snapshot':'fn(f()) { a:=[1,2,3]; g:=x=>{ a.push(x) }; print(a.map(g).stringify()); print(a.stringify()) }; f()',
'user_prefix_caught':'fn(boom(x)) { throw error("lambda body error: user") }; try { [1].map(x=>boom(x)) } catch(e) { print(e.message); print(e.code) }',
'user_prefix_uncaught':'fn(boom(x)) { throw error("lambda body error: user") }; [1].map(x=>boom(x))',
'recursive_sort_rethrow':'fn(work(n)) { a:=[3,2,1]; try { a.sort((x,y)=>{ if(n>0) { work(n-1) }; throw error("stop") }) } catch(e) { print(n); print(a.stringify()); throw e } }; try { work(2) } catch(e) { print(e.message) }',
})


CASES.update({
'wrapped_mutation_fault':'a:=[1]; print([0,a.insert(9,2)].stringify())',
'wrapped_nested_mutation_fault':'a:=[1]; print([[a.insert(9,2)]].stringify())',
'wrapped_callback_fault':'a:=[1]; print([a.map(x=>x/0)].stringify())',
'wrapped_named_callback_fault':'fn(cb(x)) { throw error("wrapped") }; print([[1].map(cb)].stringify())',
'wrapped_loop_mutation':'a:=[1]; i:=0; while(i<1) { print([a.insert(9,2)].stringify()); i+=1 }',
'wrapped_loop_callback':'i:=0; while(i<1) { print([[1].map(x=>x/0)].stringify()); i+=1 }',
'wrapped_caught_callback':'try { print([[1].map(x=>x/0)].stringify()) } catch(e) { print(e.message); print(e.code); print(e.line); print(e.column) }',
'nested_lambda_inference':'fn(f()) { g:=x=>{ h:=y=>y+1; return h(x) }; print(g(1)); print([1].map(x=>x+1).stringify()) }; f()',
})


CASES.update({
'caught_error_identity':'e:=error("identity","user.identity"); fn(cb(x)) { throw e }; try { [1].map(cb) } catch(c) { print(c==e); print(c.message); print(c.code); print(c.category); print(c.cause==null); print(c.line); print(c.column) }',
'caught_comparator_identity':'e:=error("identity","user.identity"); fn(cmp(x,y)) { throw e }; a:=[2,1]; try { a.sort(cmp) } catch(c) { print(c==e); print(c.message); print(c.code); print(c.category); print(c.cause==null); print(c.line); print(c.column) }',
'rethrow_error_identity':'e:=error("identity","user.identity"); fn(cb(x)) { try { throw e } catch(c) { print(c==e); throw c } }; try { [1].map(cb) } catch(c) { print(c==e); print(c.message); print(c.code); print(c.line); print(c.column) }',
'sort_by_unused_heterogeneous_secondary':'print([1,2,3].sort_by(x=>x,\"asc\",x=>x==2 ? \"x\" : x,\"asc\").stringify())',
'sort_by_heterogeneous_keys':'print([1,2,3].sort_by(x=>x==2 ? \"x\" : x).stringify())',
'comparator_lambda_uncaught':'a:=[2,1]; a.sort((x,y)=>x/0)',
})

CASES.update({
'aggregate_named_callback_return':'fn(cb(x)) { return x }; print([[1],[2]].map(cb).stringify())',
'aggregate_callback_wrap':'print([[1],[2]].map(x=>[x]).stringify())',
'aggregate_callback_mutation':'a:=[[1],[2]]; fn(cb(x)) { x.push(9); return x }; print(a.map(cb).stringify()); print(a.stringify())',
'object_callback_return':'fn(cb(x)) { return x }; print([{"k":1},{"k":2}].map(cb).stringify())',
})

CASES.update({
'aggregate_pop_result':'a:=[[1],[2]]; r:=a.pop(); print([r].stringify())',
'aggregate_reduce_result':'fn(cb(a,x)) { a.push(x); return a }; r:=[1,2].reduce(cb,[]); print([r].stringify())',
'aggregate_find_result':'r:=[[1],[2]].find(x=>x.size()==1); print([r].stringify())',
'aggregate_find_null':'r:=[[1],[2]].find(x=>false); print([r].stringify())',
'aggregate_min_result':'r:=[[1],[2,3]].min_by(x=>x.size()); print([r].stringify())',
})

CASES.update({
'return_comparator_result_error':'fn(f()) { a:=[2,1]; return a.sort((x,y)=>x+y) }; f()',
'return_collection_predicate_error':'fn(f()) { return [1].filter(x=>x) }; f()',
'return_mutator_error':'fn(f()) { a:=[]; return a.pop() }; f()',
'return_callback_arity_error':'fn(f()) { return [1].map((x,y)=>x) }; f()',
'return_method_arity_error':'fn(f()) { return [1].map() }; f()',
})

CASES.update({
'fast_named_helper_error':'fn(g(x)) { return x/0 }; fn(cb(x)) { return g(x) }; [1].map(cb)',
'comparator_block_return_error':'a:=[2,1]; a.sort((x,y)=>{ return x/0 })',
'script_return_kernel_error':'a:=[]; return a.pop()',
'ordinary_lambda_collection_error':'g:=()=>[1].filter(x=>x); g()',
})

def run(binary,mode,source,root):
    (root/'case.f').write_text(source+'\n');stats=root/'stats';stats.unlink(missing_ok=True)
    env=dict(os.environ,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NIFT_PREVALIDATE='off')
    p=subprocess.run([binary,'case.f'],cwd=root,env=env,capture_output=True,text=True,errors="surrogateescape",timeout=10)
    records=[json.loads(s) for s in stats.read_text().splitlines()] if stats.exists() else []
    return dict(out=p.stdout,err=p.stderr.replace(str(root),'$CASE'),exit=p.returncode,stats=records)
def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--legacy',required=True);p.add_argument('--output',required=True);a=p.parse_args();results=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-functional-') as tmp:
        for name,source in CASES.items():
            legacy=run(a.legacy,'0',source,Path(tmp));vm=run(a.nift,'require',source,Path(tmp))
            ok=all(legacy[k]==vm[k] for k in ['out','err','exit']) and vm['stats'] and vm['stats'][-1]['outcome'] in ['vm','vm-error'] and vm['stats'][-1]['legacy_executions']==0
            results.append(dict(name=name,source=source,pass_=bool(ok),legacy=legacy,vm=vm));print('PASS' if ok else 'FAIL',name)
    Path(a.output).write_text(json.dumps(dict(results=results),indent=2)+'\n');return 0 if all(r['pass_'] for r in results) else 1
if __name__=='__main__':raise SystemExit(main())
