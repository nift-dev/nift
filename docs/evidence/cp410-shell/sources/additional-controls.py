from pathlib import Path
import json,subprocess,re
p=Path('.build/cp410-shell').resolve();rows=[]
probes=json.loads(Path('.build/cp410/probes.json').read_text());selected=[x for x in probes if x['n']==2000 and x['name'] in ('map-set','json-parse-convert','json-traverse','json-mutate','json-serialize','bfs','map-identity','sort-index','sort-arithmetic')]
for x in selected:
 expected=None
 for label in ('baseline','candidate'):
  stem=f'control-{x["name"]}-{label}';cg=p/(stem+'.callgrind');log=p/(stem+'.memcheck');cmd=[str(p/(label+'-nift')),x['path']]
  r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),*cmd],capture_output=True,text=True,check=True)
  if expected is None:expected=r.stdout
  assert r.stdout==expected
  r=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--error-exitcode=99','--log-file='+str(log),*cmd],capture_output=True,text=True,check=True);assert r.stdout==expected
  s=log.read_text();assert 'ERROR SUMMARY: 0 errors' in s and 'in use at exit: 0 bytes in 0 blocks' in s
  m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',s)
  rows.append({'name':x['name'],'n':x['n'],'label':label,'instructions':int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),'allocations':int(m[1].replace(',','')),'bytes':int(m[3].replace(',','')),'stdout':expected});print(stem,'PASS',flush=True)
  Path('docs/evidence/cp410-shell/additional-control-profiles.json').write_text(json.dumps(rows,indent=2)+'\n')
print('COMPLETE',len(rows),'independent instruction/allocation controls')
