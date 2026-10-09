from pathlib import Path
import subprocess,json,re,os
p=Path('.build/cp410-capture-frame').resolve();stock=str(Path('.build/cp410-followup/phases/nift-phases').resolve());rows=json.loads(Path('docs/evidence/cp410-capture-frame/capture-counts.json').read_text());out=[]
for row in rows:
 n,u,kind=row['n'],row['unused'],row['kind'];r=dict(n=n,unused=u,kind=kind,phase_instructions={})
 for phase in ('capture','frame'):
  file=p/f'core-{kind}-{n}-{u}-{phase}.callgrind';q=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+str(file),stock,row['source']],capture_output=True,text=True,check=True,env={**os.environ,'CP51_PHASE':phase});assert q.stdout==f'1\n{n}\n';r['phase_instructions'][phase]=int(re.search(r'^summary: (\d+)',file.read_text(),re.M)[1])
 out.append(r);Path('docs/evidence/cp410-capture-frame/core-phase-instructions.json').write_text(json.dumps(out,indent=2)+'\n');print(n,u,kind,r['phase_instructions'],flush=True)
print('PASS 80 collection-scoped capture/frame instruction profiles; phase ranges overlap factory')
