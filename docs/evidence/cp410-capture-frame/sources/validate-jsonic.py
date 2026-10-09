from pathlib import Path
import json,subprocess,hashlib
p=Path('.build/cp410-capture-frame/jsonic').resolve();out=[]
cases=[p/f'{kind}-{n}.json' for n in (8,32,64,128,1000,8000) for kind in ('unique','early','middle','end','escaped')]
extra=['{"a":1,"a" BROKEN}', '{"a":1,"\\u0061": BROKEN}', '{"a":{"x":1,"x":2}}', '{"a":1,"b":"\\uD800"}', '{"a":1,}', '['*100+'0'+']'*100, '{'+','.join('"k%d":%d'%(i,i) for i in range(70))+',"k0" BROKEN}']
for i,s in enumerate(extra):q=p/f'precedence-{i}.json';q.write_text(s);cases.append(q)
for q in cases:
 for mode in ('reject','preserve') if q.name.startswith('precedence') else ('reject',):
  expected=subprocess.check_output([str(p/'probe-baseline'),str(q),mode],text=True)
  for label in ('baseline','16','32','64','128'):
   log=p/f'{q.stem}-{mode}-{label}.memcheck';r=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--error-exitcode=99','--log-file='+str(log),str(p/('probe-'+label)),str(q),mode],text=True,capture_output=True,check=True)
   assert r.stdout==expected,(q,label,r.stdout,expected)
   assert 'ERROR SUMMARY: 0 errors' in log.read_text() and 'in use at exit: 0 bytes in 0 blocks' in log.read_text()
   out.append({'case':q.name,'mode':mode,'threshold':label,'stdout':expected,'errors':0,'exit_heap_bytes':0})
  print(q.name,mode,'PASS',flush=True)
Path('docs/evidence/cp410-capture-frame/jsonic-validation.json').write_text(json.dumps(out,indent=2)+'\n')
print('PASS',len(out),'semantic + Memcheck comparisons')
