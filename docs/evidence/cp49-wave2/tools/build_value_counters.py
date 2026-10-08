"""Recompile all RV consumers with test-only lifecycle counters in an isolated tree.
Counts are diagnostic observations, never timing evidence or ABI artifacts.
"""
import pathlib,shutil,subprocess,shlex,concurrent.futures,json,os
b=pathlib.Path('.build/cp49-wave2/value-counters');b.mkdir(parents=True,exist_ok=True)
shutil.copytree('src',b/'src',dirs_exist_ok=True,ignore=shutil.ignore_patterns('*.o','*.d'))
shutil.copytree('include',b/'include',dirs_exist_ok=True)
p=b/'src/RuntimeValue.h';s=p.read_text();s=s.replace('#include <cstddef>','#include <cstddef>\n#include <atomic>\n#include <cstdio>')
code='''namespace cp49_value { struct Stats { std::atomic<unsigned long long> ctor{0}, copy_ctor{0}, move_ctor{0}, copy_assign{0}, move_assign{0}, dtor{0}; ~Stats(){std::fprintf(stderr,"value-counts ctor=%llu copy_ctor=%llu move_ctor=%llu copy_assign=%llu move_assign=%llu dtor=%llu\\n",ctor.load(),copy_ctor.load(),move_ctor.load(),copy_assign.load(),move_assign.load(),dtor.load());}};inline Stats stats;struct Token {Token(){++stats.ctor;} Token(const Token&){++stats.copy_ctor;} Token(Token&&) noexcept {++stats.move_ctor;} Token& operator=(const Token&){++stats.copy_assign;return *this;} Token& operator=(Token&&) noexcept {++stats.move_assign;return *this;} ~Token(){++stats.dtor;}};}
'''
s=s.replace('namespace nift {',code+'\nnamespace nift {',1).replace('    RuntimeValue() = default;','    cp49_value::Token lifecycle_counter;\n    RuntimeValue() = default;',1).replace(': type(other.type), num(other.num), boolean(other.boolean) {',': type(other.type), num(other.num), boolean(other.boolean), lifecycle_counter(other.lifecycle_counter) {',1).replace('        if (this != &other) {','        if (this != &other) {\n            lifecycle_counter=other.lifecycle_counter;',1);p.write_text(s)
plan=subprocess.check_output(['make','-n','-B','nift'],text=True);commands=[];link=None;replacement={}
for line in plan.splitlines():
 if not line.startswith('g++ '):continue
 args=shlex.split(line)
 if '-c' in args:
  source=args[args.index('-c')+1]
  if not source.startswith('src/'):continue
  original=args[args.index('-o')+1];obj=str(b/(original+'.o'));pathlib.Path(obj).parent.mkdir(parents=True,exist_ok=True);replacement[original]=obj
  args[args.index('-c')+1]=str(b/source);args[args.index('-o')+1]=obj;args.insert(1,'-I'+str(b/'src'));commands.append(args)
 else:link=args
assert link and commands
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for result in pool.map(lambda args:subprocess.run(args,check=True),commands):pass
link=[replacement.get(x,x) for x in link];link[link.index('-o')+1]=str(b/'nift-counter');subprocess.run(link,check=True);(b/'build.json').write_text(json.dumps(commands+[link],indent=2)+'\n')
rows=json.load(open('.build/cp49/probes.json'))+json.load(open('.build/cp49-campaign/extra-probes.json'))+json.load(open('.build/cp49-wave2/probes.json'));counts=[]
for r in rows:
 if r['n'] not in [0,2000]:continue
 q=subprocess.run([str((b/'nift-counter').resolve()),r['path']],capture_output=True,text=True,check=True);assert '\n'.join(q.stdout.splitlines()[:-1])==r['expected'];counts.append(dict(name=r['name'],counts=q.stderr))
(b/'counts.json').write_text(json.dumps(counts,indent=2)+'\n')
