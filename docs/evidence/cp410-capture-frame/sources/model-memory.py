from pathlib import Path
import subprocess,json
p=Path('.build/cp410-capture-frame').resolve();rows=[]
for scenario in ('identity','captured','changing','nested','local','missing'):
 for u in (0,100):
  expected=None
  for model in ('current','shared','parent','cow','memo'):
   if scenario=='changing' and model=='parent':continue
   cmd=[str(p/'env-model-extra'),model,scenario,'16000',str(u),'16'];f=p/'model-rss.time';r=subprocess.run(['/usr/bin/time','-f','%M','-o',str(f),*cmd],capture_output=True,text=True,check=True)
   if expected is None:expected=r.stdout
   assert r.stdout==expected
   row={'model':model,'scenario':scenario,'unused':u,'n':16000,'reads':16,'rss_kib':int(f.read_text()),'stdout':expected};rows.append(row)
   # Exercise all models through the expanded lookup/cache-miss paths with Memcheck.
   log=p/f'memory-{model}-{scenario}-{u}.memcheck';r=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--error-exitcode=99','--log-file='+str(log),str(p/'env-model-extra'),model,scenario,'2000',str(u),'16'],capture_output=True,text=True,check=True);s=log.read_text();assert 'ERROR SUMMARY: 0 errors' in s and 'in use at exit: 0 bytes in 0 blocks' in s
   row['memcheck_errors']=0;row['memcheck_n']=2000
   Path('docs/evidence/cp410-capture-frame/model-memory.json').write_text(json.dumps(rows,indent=2)+'\n')
  print(scenario,u,'PASS',flush=True)
print('COMPLETE model peak RSS and extended Memcheck cases',len(rows))
