from pathlib import Path
import subprocess,json,re,os
p=Path('.build/cp410-implementation').resolve();rows=[]
def balanced(n):
 if n==1:return 'm'
 return '('+balanced(n//2)+'+'+balanced(n-n//2)+')'
for repetitions in (1,8,32,128,256,1024):
 source='f := x => '+balanced(repetitions)+'\nm := 7; a := []; i := 0; while(i<1000) { a.push(i); i+=1 }; r := a.map(f); print(r[0]); print(r.length())\n'
 path=p/f'misses-{repetitions}.f';path.write_text(source)
 for label,binary in [('baseline',p/'baseline-nift'),('snapshot',Path('nift').resolve())]:
  cg=p/f'misses-{repetitions}-{label}.callgrind';q=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(binary),str(path)],capture_output=True,text=True,check=True);assert q.stdout==f'{7*repetitions}\n1000\n';rows.append(dict(repetitions=repetitions,label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1])))
 q=subprocess.run([str(Path('.build/nift-lambda-cache-guard').resolve()),str(path)],capture_output=True,text=True,env={**os.environ,'NIFT_TEST_LAMBDA_CACHE_STATS':'1'},check=True)
 counters=re.search(r'^callback-overlay (.*)$',q.stderr,re.M)[1];rows[-1]['counters']={key:int(value) for key,value in re.findall(r'(\w+)=(\d+)',counters)};print(repetitions,rows[-2:],flush=True);(p/'misses-instructions.json').write_text(json.dumps(rows,indent=2)+'\n')
