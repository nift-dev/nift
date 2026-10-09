from pathlib import Path
import subprocess,json,re,os,shutil
p=Path('.build/cp410-capture-frame').resolve();shutil.copy2(p/'env-model.cpp',p/'env-model-v2.cpp');shutil.copy2(p/'env-model',p/'env-model-v2')
s=(p/'env-model.cpp').read_text();s=s.replace('for(std::size_t i=0;i<n;++i){std::shared_ptr<Env> env;', 'for(std::size_t i=0;i<n;++i){if(scenario=="changing") source.at("offset").rebind(std::make_shared<nift::RuntimeValue>(static_cast<double>(i)));std::shared_ptr<Env> env;')
s=s.replace('frame.finish();\n    {cp51::Phase body', 'frame.finish(); if(scenario=="local")f.local.emplace("local",binding(7));\n    {cp51::Phase body')
s=s.replace('scenario=="global"?"cmd":"offset"','scenario=="global"?"cmd":scenario=="local"?"local":"offset"')
s=s.replace('for(int i=0;i<100;++i)(*rootbinding.slot)->array.emplace_back(i);','for(int i=0;i<100;++i){(*rootbinding.slot)->array.emplace_back(i);}');s=s.replace('auto layer=std::make_shared<Node>();copy(layer->local,local);layer->parent=env->parent;','auto layer=std::make_shared<Node>();for(const auto& b:*caller)if(!capture_find(*env,b.first))layer->local.emplace(b);copy(layer->local,local);layer->parent=env->parent;').replace('}else{if(env->snapshot)copy(out->flat,*env->snapshot);','}else{copy(out->flat,*caller);if(env->snapshot)copy(out->flat,*env->snapshot);').replace('assert(next.find("offset")->value->num==7);','assert(next.find("offset")->value->num==7);caller.erase("late");caller.emplace("late",binding(22));assert(next.find("late")->value->num==11);');(p/'env-model-extra.cpp').write_text(s)
subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-I'+str(p),'-I'+str(p/'phases'),'-Isrc','-Iinclude','-Ivendor',str(p/'env-model-extra.cpp'),str(p/'phases/Alloc.cpp'),'src/RuntimeValue.o','-o',str(p/'env-model-extra')],check=True)
rows=[]
configs=[(s,u,reads,phase) for s in ('identity','local','captured','missing','global') for u in (0,100) for reads in (1,16,256) for phase in ('factory',)]
configs += [(s,u,1,phase) for s in ('changing','nested','identity','captured') for u in (0,100) for phase in ('factory','destruction')]
for scenario,u,reads,phase in configs:
 expected=None
 for model in ('current','shared','parent','cow','memo'):
  if scenario=='changing' and model=='parent':continue # immutable-parent optimistic model cannot update root metadata
  stem=f'extra-{model}-{scenario}-{u}-{reads}-{phase}';cg=p/(stem+'.callgrind');cmd=[str(p/'env-model-extra'),model,scenario,'2000',str(u),str(reads)]
  r=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+str(cg),*cmd],capture_output=True,text=True,check=True,env={**os.environ,'CP51_PHASE':phase})
  if expected is None:expected=r.stdout
  assert r.stdout==expected,(stem,r.stdout,expected)
  row={'model':model,'scenario':scenario,'unused':u,'reads':reads,'phase':phase,'n':2000,'instructions':int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),'stdout':r.stdout};rows.append(row)
  Path('docs/evidence/cp410-capture-frame/model-extra.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(scenario,u,reads,phase,'PASS',flush=True)
print('PASS lookup/mutation/teardown instruction profiles',len(rows))
