from pathlib import Path
import subprocess,json,re
p=Path(__file__).parent.resolve();old=Path('.build/cp410-capture-frame/cases').resolve();rows=[]
for n,u,kind in [(16000,0,'identity'),(16000,100,'identity'),(16000,0,'captured'),(16000,100,'captured')]:
 source=old/f'n{n:06}'/f'u{u:03}'/f'{kind:_<12}'/'official-equivalent.f';expected=None
 for label,binary in [('baseline',p/'string-nift'),('snapshot',p/'snapshot/nift')]:
  cg=p/f'sort-{n}-{u}-{kind}-{label}.callgrind';r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(binary),str(source)],capture_output=True,text=True,check=True)
  if expected is None:expected=r.stdout
  assert r.stdout==expected
  rows.append(dict(n=n,unused=u,kind=kind,label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),source=str(source),stdout=r.stdout));print(rows[-1],flush=True);(p/'snapshot-profiles.json').write_text(json.dumps(rows,indent=2)+'\n')
