from pathlib import Path
import subprocess,json
p=Path('.build/cp410-shell').resolve();rows=[]
for n in (1000,100000):
 root=p/f'official-traverse-{n}';code=(root/'source.f').read_text()
 for label in ('baseline','candidate'):
  log=p/f'final-{label}-{n}.strace';r=subprocess.run(['strace','-c','-o',str(log),str(p/(label+'-nift')),'-e',code],cwd=root,capture_output=True,text=True,check=True)
  assert r.stdout.strip()==str(n+n//5)
  rows.append({'label':label,'n':n,'matches':n+n//5,'summary':log.read_text()});print(label,n,'PASS',flush=True)
Path('docs/evidence/cp410-shell/final-syscalls.json').write_text(json.dumps(rows,indent=2)+'\n')
