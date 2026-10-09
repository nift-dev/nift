from pathlib import Path
import subprocess,json,time,resource,statistics
p=Path('.build/cp410-shell').resolve();rows=[]
def pair(name,root,code,expected,reps=4):
 for rep in range(reps):
  for label in (('baseline','candidate') if rep%2==0 else ('candidate','baseline')):
   f=p/'resource.time';usage=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.monotonic();r=subprocess.run(['/usr/bin/time','-f','%M','-o',str(f),str(p/(label+'-nift')),'-e',code],cwd=root,capture_output=True,text=True,check=True);wall=time.monotonic()-t;used=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.stdout.strip()==str(expected)
   row={'name':name,'repeat':rep,'label':label,'wall':wall,'cpu':used.ru_utime+used.ru_stime-usage.ru_utime-usage.ru_stime,'rss_kib':int(f.read_text())};rows.append(row);print(row,flush=True)
for n in (1000,10000,100000):
 root=p/f'official-traverse-{n}';pair(f'official-{n}',root,(root/'source.f').read_text(),n+max(20000 if n==100000 else n//5,1))
for shape in ('flat','deep','mixed','short'):
 root=Path((p/'short-root.txt').read_text()) if shape=='short' else p/f'{shape}-100000'
 for pattern in ('files/**/*.dat','files/**/*.missing','files/**/obj-*000.dat'):
  code='print(ls('+json.dumps(pattern)+').size())';expected=subprocess.check_output([str(p/'baseline-nift'),'-e',code],cwd=root,text=True).strip();pair(shape+'-'+pattern,root,code,expected)
for n in (5000,50000):pair(f'string-{n}',p,f's := "alpha"; i := 0; while(i < {n}) {{ s="alpha".to_upper().replace("A","a"); i+=1 }}; print(s)','aLPHa',8)
Path('docs/evidence/cp410-shell/final-timing.json').write_text(json.dumps(rows,indent=2)+'\n')
print('COMPLETE final paired timing')
