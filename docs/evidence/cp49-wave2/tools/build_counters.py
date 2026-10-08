"""Build a separate diagnostic CLI; never link counters into production."""
import pathlib,shlex,subprocess,json,os
b=pathlib.Path('.build/cp49-wave2/counters');b.mkdir(parents=True,exist_ok=True)
header='''#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
namespace cp49 { struct Stats { std::atomic<unsigned long long> expressions{0}, recursive_compat{0}, prepared_calls{0}, scopes{0}, captures{0}, source_contexts{0}, cached_paths{0}, source_documents{0}; std::mutex mutex; std::map<std::string,unsigned long long> shapes; void shape(const std::string& s){std::lock_guard<std::mutex> lock(mutex);++shapes[s];} ~Stats(){std::fprintf(stderr,"cp49 expressions=%llu recursive_compat=%llu prepared_calls=%llu scopes=%llu captures=%llu source_contexts=%llu cached_paths=%llu source_documents=%llu\\n",expressions.load(),recursive_compat.load(),prepared_calls.load(),scopes.load(),captures.load(),source_contexts.load(),cached_paths.load(),source_documents.load());for(const auto& item:shapes)std::fprintf(stderr,"legacy-shape %llu %s\\n",item.second,item.first.c_str());}};inline Stats stats; }
'''
(b/'Counters.h').write_text(header)
changes={
'ParserExpression.cpp': [('eval = [&](const nift::detail::SourceText& raw, nift::RuntimeValue& out, int depth) -> bool {', 'eval = [&](const nift::detail::SourceText& raw, nift::RuntimeValue& out, int depth) -> bool { ++cp49::stats.recursive_compat;'), ('    last_expression_mutation_ = false;', '    ++cp49::stats.expressions;\n    last_expression_mutation_ = false;'),('for(const auto& scope:variable_scopes_) for(const auto& kv:scope) li->captures[kv.first]=kv.second;', 'for(const auto& scope:variable_scopes_) for(const auto& kv:scope) {++cp49::stats.captures; li->captures[kv.first]=kv.second;}')],
'ParserTemplate.cpp': [('return evaluate_expression(x,out,e,prepared_view->slice(', 'cp49::stats.shape(x);return evaluate_expression(x,out,e,prepared_view->slice('),('auto lexical_env=enter_lexical_environment(callee_env ? callee_env : callee->module_env);','++cp49::stats.prepared_calls;auto lexical_env=enter_lexical_environment(callee_env ? callee_env : callee->module_env);')],
'ParserScript.cpp':[('if(!input_view) input_view=SourceView::identity({},source);','if(!input_view) {++cp49::stats.source_documents;input_view=SourceView::identity({},source); }'),('if(!view)view=nift::detail::SourceView::identity(source_path,source);','if(!view){++cp49::stats.source_documents;view=nift::detail::SourceView::identity(source_path,source);}')],
'Parser.cpp':[('void Parser::push_variable_scope() { variable_scopes_.emplace_back(); }','void Parser::push_variable_scope() { ++cp49::stats.scopes;variable_scopes_.emplace_back(); }')]
}
for name,replacements in changes.items():
 s=pathlib.Path('src',name).read_text()
 for old,new in replacements:assert old in s,(name,old);s=s.replace(old,new)
 s=s.replace('source_context_stack_.push_back(', '++cp49::stats.source_contexts;source_context_stack_.push_back(')
 s=s.replace('prep.source_path=std::make_shared', '++cp49::stats.cached_paths;prep.source_path=std::make_shared')
 s=s.replace('if (!view) view = nift::detail::SourceView::identity(source_path, input);', 'if (!view) {++cp49::stats.source_documents;view = nift::detail::SourceView::identity(source_path, input);}')
 (b/name).write_text('#include "Counters.h"\n'+s)
plan=subprocess.check_output(['make','-n',*[v for name in changes for v in ['-W','src/'+name]],'nift'],text=True);commands=[]
for line in plan.splitlines():
 if not line.startswith('g++ '):continue
 args=shlex.split(line)
 if '-c' in args:
  name=next((name for name in changes if 'src/'+name in args),None)
  if not name:continue
  args[args.index('src/'+name)]=str(b/name);args[args.index('-o')+1]=str(b/(name+'.o'));args.insert(1,'-DNIFT_TEST_LAMBDA_CACHE_STATS')
 else:
  args=[str(b/(name+'.o')) if x=='src/'+name[:-4]+'.o' else x for x in args for name in [next((name for name in changes if x=='src/'+name[:-4]+'.o'),'')]]
  args[args.index('-o')+1]=str(b/'nift-counter')
 commands.append(args);subprocess.run(args,check=True)
(b/'build.json').write_text(json.dumps(commands,indent=2)+'\n')
rows=json.load(open('.build/cp49/probes.json'))+json.load(open('.build/cp49-campaign/extra-probes.json'))+json.load(open('.build/cp49-wave2/probes.json'));results=[]
for r in rows:
 if r['n'] not in [0,2000]:continue
 p=subprocess.run([str((b/'nift-counter').resolve()),r['path']],env={**os.environ,'NIFT_TEST_LAMBDA_CACHE_STATS':'1'},capture_output=True,text=True,check=True)
 assert '\n'.join(p.stdout.splitlines()[:-1])==r['expected']
 (b/(r['name']+'.log')).write_text(p.stderr);results.append({'name':r['name'],'counters':p.stderr})
(b/'counts.json').write_text(json.dumps(results,indent=2)+'\n')
