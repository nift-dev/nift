from pathlib import Path
import json,subprocess,re
p=Path('.build/cp410-implementation/filesystem').resolve();rows=[]
probes=json.loads(Path('.build/cp410/probes.json').read_text())
for name in ('loops','call-scalar','call-noarg','call-closure','call-callback','bfs','json-transform','json-traverse','map-set'):
 choices=[r for r in probes if r['name']==name]
 if not choices:continue
 row=max(choices,key=lambda r:r['n']);expected=None
 for label,binary in [('accepted',p/'accepted-nift'),('recipe',p/'nift')]:
  cg=p/f'control-{name}-{label}.callgrind';q=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(binary),row['path']],capture_output=True,text=True,check=True)
  if expected is None:expected=q.stdout
  assert q.stdout==expected;rows.append(dict(name=name,n=row['n'],label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1])))
 print(name,rows[-2:],flush=True);(p/'controls-instructions.json').write_text(json.dumps(rows,indent=2)+'\n')
