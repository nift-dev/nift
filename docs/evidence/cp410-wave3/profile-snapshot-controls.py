from pathlib import Path
import subprocess,json,re
p=Path(__file__).parent.resolve();probes=json.loads(Path('.build/cp410/probes.json').read_text());rows=[]
cases=[x for x in probes if x['n']==8000 and x['name'] in ('loops','call-scalar','call-closure','call-callback','map-set','json-parse-convert','json-traverse','json-mutate','json-serialize','bfs','sort-identity')]
for x in cases:
 expected=None
 for label,binary in [('baseline',p/'string-nift'),('snapshot',p/'snapshot-v2-nift')]:
  cg=p/f"control-{x['name']}-{label}.callgrind";r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(binary),x['path']],capture_output=True,text=True,check=True)
  if expected is None:expected=r.stdout
  assert r.stdout==expected
  log=p/f"control-{x['name']}-{label}.memcheck";r=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--error-exitcode=99','--log-file='+str(log),str(binary),x['path']],capture_output=True,text=True,check=True);assert r.stdout==expected;s=log.read_text();assert 'ERROR SUMMARY: 0 errors' in s and 'in use at exit: 0 bytes in 0 blocks' in s;m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',s)
  rows.append(dict(name=x['name'],label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),allocations=int(m[1].replace(',','')),bytes=int(m[3].replace(',','')),memcheck_errors=0));(p/'snapshot-controls.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(x['name'],'PASS',flush=True)
