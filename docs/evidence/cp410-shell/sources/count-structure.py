from pathlib import Path
import subprocess,json,re
p=Path('.build/cp410-shell').resolve();rows=[]
for shape in ('official-traverse','deep','mixed'):
 for n in (1000,10000,100000):
  root=p/f'{shape}-{n}'
  for pattern in ('files/**/*.dat','files/**/*.missing','files/**/obj-*000*.dat'):
   code='print(ls('+json.dumps(pattern)+').size())';r=subprocess.run([str(p/'counter-nift'),'-e',code],cwd=root,capture_output=True,text=True,check=True);m=re.search('glob-investigation (.*)',r.stderr);assert m,r.stderr
   counts={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',m[1])};rows.append({'shape':shape,'n':n,'pattern':pattern,'results':int(r.stdout),'counts':counts})
Path('docs/evidence/cp410-shell/structural-counts.json').write_text(json.dumps(rows,indent=2)+'\n');print('PASS',len(rows),'structural traversal profiles')
