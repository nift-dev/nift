from pathlib import Path
import subprocess,json,re
p=Path('.build/cp410-shell').resolve();rows=[]
root=p/'official-create-small-1000';code=(root/'source.f').read_text();names=(root/'targets.txt').read_text().splitlines();payload=('0123456789abcdef'*7+'0123456789abcde\n').encode()
for label in ('baseline','candidate'):
 for tool in ('callgrind','memcheck','strace'):
  for name in names:(root/name).unlink(missing_ok=True)
  cmd=[str(p/(label+'-nift')),'-e',code];stem=f'final-save-{label}';log=p/f'{stem}-{tool}.log'
  if tool=='strace':args=['strace','-c','-o',str(log)]
  else:
   args=['valgrind','--tool='+tool,'--log-file='+str(log)];args+=['--callgrind-out-file='+str(p/(stem+'.callgrind'))] if tool=='callgrind' else ['--leak-check=full','--error-exitcode=99']
  r=subprocess.run([*args,*cmd],cwd=root,capture_output=True,text=True,check=True);assert r.stdout=='OK\n';assert all((root/name).read_bytes()==payload for name in names)
  if tool=='callgrind':rows.append({'label':label,'instructions':int(re.search(r'^summary: (\d+)',(p/(stem+'.callgrind')).read_text(),re.M)[1])})
  if tool=='memcheck':
   s=log.read_text();assert 'ERROR SUMMARY: 0 errors' in s and 'in use at exit: 0 bytes in 0 blocks' in s;m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',s);rows[-1].update(allocations=int(m[1].replace(',','')),bytes=int(m[3].replace(',','')),memcheck_errors=0)
  if tool=='strace':rows[-1]['syscalls']=log.read_text()
  print(label,tool,'PASS',flush=True)
Path('docs/evidence/cp410-shell/final-save-profile.json').write_text(json.dumps(rows,indent=2)+'\n')
