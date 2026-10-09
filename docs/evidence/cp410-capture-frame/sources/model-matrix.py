from pathlib import Path
import subprocess,re,json,os
p=Path('.build/cp410-capture-frame').resolve();binary=str(p/'env-model');rows=[]
for scenario in ('identity','captured','reused-identity','reused','mutation','nested','missing','global'):
 for unused in (0,5,16,50,100):
  for model in ('current','shared','parent','cow'):
   cmd=[binary,model,scenario,'2000',str(unused),'1'];stem=f'model-{model}-{scenario}-{unused}';cg=p/(stem+'.callgrind');log=p/(stem+'.memcheck')
   r=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+str(cg),*cmd],capture_output=True,text=True,check=True,env={**os.environ,'CP51_PHASE':'factory'})
   instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]);phases={m[0]:{'allocations':int(m[2]),'bytes':int(m[3]),'frees':int(m[4])} for m in re.findall(r'phase (\w+) calls=(\d+) allocations=(\d+) bytes=(\d+) frees=(\d+)',r.stderr)}
   expected=r.stdout
   r=subprocess.run(['valgrind','--tool=memcheck','--error-exitcode=99','--leak-check=full','--log-file='+str(log),*cmd],capture_output=True,text=True,check=True);assert r.stdout==expected;assert 'ERROR SUMMARY: 0 errors' in log.read_text() and 'in use at exit: 0 bytes in 0 blocks' in log.read_text()
   row={'model':model,'scenario':scenario,'unused':unused,'n':2000,'reads':1,'instructions':instructions,'stdout':expected,'phases':phases};rows.append(row);Path('docs/evidence/cp410-capture-frame/models.json').write_text(json.dumps(rows,indent=2)+'\n');print(model,scenario,unused,instructions,phases['factory'],flush=True)
print('PASS 160 semantic-checked environment micro/prototypes')
