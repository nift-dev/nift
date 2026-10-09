from pathlib import Path
import subprocess,json,re,os
p=Path('.build/cp410-capture-frame').resolve();subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(p),'-I'+str(p/'phases'),'-Isrc','-Iinclude',str(p/'env-model-extra.cpp'),'src/RuntimeValue.o','-o',str(p/'env-model-plain')],check=True);rows=[]
for scenario in ('identity','captured','changing','reused-identity','reused','missing','local','global','nested'):
 for u in (0,100):
  expected=None
  for model in ('current','shared','parent','cow','memo'):
   if scenario=='changing' and model=='parent':continue
   stem=f'plain-{model}-{scenario}-{u}';cg=p/(stem+'.callgrind');r=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+str(cg),str(p/'env-model-plain'),model,scenario,'2000',str(u),'1'],capture_output=True,text=True,check=True,env={**os.environ,'CP51_PHASE':'factory'})
   if expected is None:expected=r.stdout
   assert r.stdout==expected
   rows.append({'model':model,'scenario':scenario,'unused':u,'n':2000,'reads':1,'instructions':int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),'stdout':expected})
   Path('docs/evidence/cp410-capture-frame/plain-models.json').write_text(json.dumps(rows,indent=2)+'\n')
  print(scenario,u,'PASS',flush=True)
print('COMPLETE unhooked allocation instruction models',len(rows))
