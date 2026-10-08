"""Isolated expression-TU instrumentation; production objects remain untouched.
Phase instruction counts include instrumentation. Allocation counters count C++
new requests, not all libc allocations. Nested phases are inclusive.
"""
import pathlib,subprocess,shlex,json
b=pathlib.Path('.build/cp49-identity/phases');b.mkdir(parents=True,exist_ok=True)
header=r'''#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <valgrind/callgrind.h>
namespace cp51 {
enum Id {Factory,Instance,Metadata,Capture,Registration,Arguments,Frame,Body,Key,Decoration,Sorting,Result,Cleanup,ReturnKey,Destruction,Count};
inline const char* names[]={"factory","instance","metadata","capture","registration","arguments","frame","body","key","decoration","sorting","result","cleanup","returned_key","destruction"};
struct Entry {unsigned long long calls=0,allocations=0,bytes=0;unsigned depth=0;};
struct Stats {Entry entries[Count]; unsigned long long capture_entries=0,frame_capture_entries=0,comparisons=0,instances=0; const char* selected=std::getenv("CP51_PHASE");unsigned selected_depth=0;
~Stats(){for(int i=0;i<Count;++i)std::fprintf(stderr,"phase %s calls=%llu allocations=%llu bytes=%llu\n",names[i],entries[i].calls,entries[i].allocations,entries[i].bytes);std::fprintf(stderr,"events instances=%llu capture_entries=%llu frame_capture_entries=%llu comparisons=%llu\n",instances,capture_entries,frame_capture_entries,comparisons);}};
inline Stats stats;
inline void allocation(std::size_t n){for(int i=0;i<Count;++i)if(stats.entries[i].depth){++stats.entries[i].allocations;stats.entries[i].bytes+=n;}}
struct Phase {Id id;bool open=true,selected=false;Phase(Id x):id(x){++stats.entries[id].calls;++stats.entries[id].depth;selected=stats.selected&&std::strcmp(stats.selected,names[id])==0;if(selected&&stats.selected_depth++==0){CALLGRIND_TOGGLE_COLLECT;}}
void finish(){if(!open)return;if(selected&&--stats.selected_depth==0){CALLGRIND_TOGGLE_COLLECT;}--stats.entries[id].depth;open=false;}~Phase(){finish();}};
template<Id id,class F> __attribute__((noinline)) auto run(F&& f)->decltype(f()){Phase phase(id);return f();}
}
'''
(b/'Phases.h').write_text(header)
alloc=r'''#include "Phases.h"
void* operator new(std::size_t n){cp51::allocation(n);if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){cp51::allocation(n);void* p=nullptr;if(posix_memalign(&p,static_cast<std::size_t>(a),n?n:1)==0)return p;throw std::bad_alloc();}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t) noexcept{std::free(p);}void operator delete[](void*p,std::align_val_t) noexcept{std::free(p);}
void operator delete(void*p,std::size_t,std::align_val_t) noexcept{std::free(p);}void operator delete[](void*p,std::size_t,std::align_val_t) noexcept{std::free(p);}
'''
(b/'Alloc.cpp').write_text(alloc)
s=pathlib.Path('src/ParserExpression.cpp').read_text()
def replace(a,c,count=None):
 global s
 assert a in s,a
 if count is not None:assert s.count(a)==count,(a,s.count(a))
 s=s.replace(a,c)
replace('auto instantiate_lambda = [&](const LambdaSyntax& code) {','auto instantiate_lambda = [&](const LambdaSyntax& code) { ++cp51::stats.instances;')
replace('auto li = std::make_shared<LambdaInstance>();','auto li = cp51::run<cp51::Instance>([&](){return std::make_shared<LambdaInstance>();}); {cp51::Phase phase(cp51::Metadata);',1)
replace('for(const auto& scope:variable_scopes_) for(const auto& kv:scope) li->captures[kv.first]=kv.second;','} {cp51::Phase phase(cp51::Capture);for(const auto& scope:variable_scopes_) for(const auto& kv:scope) {++cp51::stats.capture_entries;li->captures[kv.first]=kv.second;}}',1)
replace('li->module_env=active_module_env_ ? active_module_env_ : loading_module_env_;','cp51::Phase registration(cp51::Registration);li->module_env=active_module_env_ ? active_module_env_ : loading_module_env_;',1)
replace('nift::RuntimeValue cb;if(!eval(cbexpr,cb,depth+1))return false;', 'nift::RuntimeValue cb;if(!cp51::run<cp51::Factory>([&](){return eval(cbexpr,cb,depth+1);}))return false;')
replace('nift::RuntimeValue cbv;if(!eval(args[0],cbv,depth+1))return false;','nift::RuntimeValue cbv;if(!cp51::run<cp51::Factory>([&](){return eval(args[0],cbv,depth+1);}))return false;',1)
replace('const nift::detail::SourceView& body_view) {\n            auto legacy', 'const nift::detail::SourceView& body_view) {cp51::Phase body_phase(cp51::Body);\n            auto legacy',1)
# Only the canonical value callback block is delimited; indirect calls are controls.
a=s.index('auto invoke_value_cb=');z=s.index('auto invoke_value_callback=',a);part=s[a:z]
part=part.replace('auto lexical_env=enter_lexical_environment(fn->module_env);','cp51::Phase frame_phase(cp51::Frame);auto lexical_env=enter_lexical_environment(fn->module_env);')
part=part.replace('for(const auto& kv:fn->captures)sc[kv.first]=kv.second;','for(const auto& kv:fn->captures){++cp51::stats.frame_capture_entries;sc[kv.first]=kv.second;}')
part=part.replace('if(fn->block){','frame_phase.finish();if(fn->block){',1)
part=part.replace('source_context_stack_.pop_back();','cp51::Phase cleanup_phase(cp51::Cleanup);source_context_stack_.pop_back();',1)
s=s[:a]+part+s[z:]
replace('std::vector<nift::RuntimeValue> callback_value_arguments(const nift::RuntimeValue& first) {','std::vector<nift::RuntimeValue> callback_value_arguments(const nift::RuntimeValue& first) {cp51::Phase args_phase(cp51::Arguments);',1)
replace('d.keys.push_back(std::move(k));','cp51::run<cp51::Key>([&](){d.keys.push_back(std::move(k));});',1)
replace('d.value=std::move(v);decorated.push_back(std::move(d));','cp51::run<cp51::Decoration>([&](){d.value=std::move(v);decorated.push_back(std::move(d));});',1)
replace('bool compare_error=false;std::stable_sort(decorated.begin(),decorated.end(),','bool compare_error=false;cp51::Phase sorting_phase(cp51::Sorting);std::stable_sort(decorated.begin(),decorated.end(),',1)
replace('for(size_t si=0;si<specs.size();++si){int c=0;if(!cmp_key','++cp51::stats.comparisons;for(size_t si=0;si<specs.size();++si){int c=0;if(!cmp_key',1)
replace('});if(compare_error)return false;out=nift::RuntimeValue::make_array();out.array.reserve(decorated.size());for(auto& d:decorated)out.array.push_back(std::move(d.value));return true;','});sorting_phase.finish();if(compare_error)return false;cp51::Phase result_phase(cp51::Result);out=nift::RuntimeValue::make_array();out.array.reserve(decorated.size());for(auto& d:decorated)out.array.push_back(std::move(d.value));return true;',1)
replace('result = *found->second.value;', 'cp51::run<cp51::ReturnKey>([&](){result = *found->second.value;});',1)
replace('Decorated d;d.keys.reserve(specs.size());','Decorated d;cp51::run<cp51::Key>([&](){d.keys.reserve(specs.size());});',1)
replace('for(auto& d:decorated)out.array.push_back(std::move(d.value));return true;', 'for(auto& d:decorated)out.array.push_back(std::move(d.value));result_phase.finish();cp51::run<cp51::Destruction>([&](){decorated.clear();});return true;',1)
replace('cp51::Phase registration(cp51::Registration);li->module_env=', 'if(cp51::stats.instances==1){for(const auto& kv:li->captures)std::fprintf(stderr,"capture-name %s location=%d\\n",kv.first.c_str(),kv.second.is_location_ref());}cp51::Phase registration(cp51::Registration);li->module_env=',1)
(b/'ParserExpression.cpp').write_text('#include "Phases.h"\n'+s)
plan=subprocess.check_output(['make','-n','-W','src/ParserExpression.cpp','nift'],text=True);commands=[]
for line in plan.splitlines():
 if not line.startswith('g++ '):continue
 args=shlex.split(line)
 if '-c' in args:
  if 'src/ParserExpression.cpp' not in args:continue
  args[args.index('src/ParserExpression.cpp')]=str(b/'ParserExpression.cpp');args[args.index('-o')+1]=str(b/'ParserExpression.o');args.insert(1,'-DNIFT_TEST_LAMBDA_CACHE_STATS');args.insert(1,'-g')
  allocargs=['g++','-std=c++17','-O2','-g','-c',str(b/'Alloc.cpp'),'-o',str(b/'Alloc.o')];subprocess.run(allocargs,check=True);commands.append(allocargs)
 else:
  args=[str(b/'ParserExpression.o') if x=='src/ParserExpression.o' else x for x in args];args.insert(1,str(b/'Alloc.o'));args[args.index('-o')+1]=str(b/'nift-phases')
 subprocess.run(args,check=True);commands.append(args)
(b/'build.json').write_text(json.dumps(commands,indent=2)+'\n')
print('Isolated diagnostic binary built.',flush=True)
