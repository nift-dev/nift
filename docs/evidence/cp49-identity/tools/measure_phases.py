"""Inclusive stage measurements from isolated instrumentation, not production timings."""
import pathlib,subprocess,os,re,json
b=pathlib.Path('.build/cp49-identity');binary=str((b/'phases/nift-phases').resolve())
rows=json.loads((b/'probes.json').read_text());stages=['factory','instance','metadata','capture','registration','arguments','frame','body','key','decoration','sorting','result','cleanup','returned_key','destruction'];results=[]
for r in rows:
 q=subprocess.run([binary,r['path']],capture_output=True,text=True,env={**os.environ,'NIFT_TEST_LAMBDA_CACHE_STATS':'1'},timeout=30,check=True);assert '\n'.join(q.stdout.splitlines()[:-1])==r['expected'];(b/(r['name']+'-phase-counters.log')).write_text(q.stderr)
 counts={m[1]:dict(calls=int(m[2]),allocations=int(m[3]),bytes=int(m[4])) for m in re.finditer(r'phase (\w+) calls=(\d+) allocations=(\d+) bytes=(\d+)',q.stderr)}
 item=dict(name=r['name'],counters=q.stderr,stages=counts)
 # All six selector forms receive stage profiles; residual calls receive counters.
 if r['name'] not in ['call-scalar','call-closure','call-callback','sort-unused-50']:
  for stage in stages:
   if not counts[stage]['calls']:counts[stage]['instructions']=0;continue
   path=b/(r['name']+'-'+stage+'.callgrind');q=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+str(path),binary,r['path']],env={**os.environ,'CP51_PHASE':stage},capture_output=True,text=True,timeout=300,check=True);assert '\n'.join(q.stdout.splitlines()[:-1])==r['expected'];counts[stage]['instructions']=int(re.search(r'^summary: (\d+)',path.read_text(),re.M)[1])
 results.append(item);(b/'phase-metrics.json').write_text(json.dumps(results,indent=2)+'\n');print(r['name'],'PASS',flush=True)
