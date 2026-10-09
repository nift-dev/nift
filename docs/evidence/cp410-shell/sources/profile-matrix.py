from pathlib import Path
import subprocess,json,re
p=Path('.build/cp410-shell').resolve();rows=[]
for n in (1000,10000,100000):
 root=p/f'official-traverse-{n}';code=(root/'source.f').read_text()
 for label in ('baseline','candidate','native'):
  cmd=[str(p/'native')] if label=='native' else [str(p/(label+'-nift')),'-e',code];stem=f'official-{n}-{label}'
  for tool in ('callgrind','memcheck'):
   args=['valgrind','--tool='+tool,'--log-file='+str(p/(stem+'-'+tool+'.log'))];args+=['--callgrind-out-file='+str(p/(stem+'.callgrind'))] if tool=='callgrind' else ['--leak-check=full','--error-exitcode=99']
   r=subprocess.run([*args,*cmd],cwd=root,capture_output=True,text=True,check=True);assert r.stdout.strip()==str(n+max(20000 if n==100000 else n//5,1))
   print('PASS',stem,tool,flush=True)
  c=(p/(stem+'-callgrind.log')).read_text();m=(p/(stem+'-memcheck.log')).read_text();rows.append({'n':n,'label':label,'instructions':int(re.search(r'I\s+refs:\s+([\d,]+)',c).group(1).replace(',','')),'heap':re.search(r'total heap usage: (.*)',m).group(1),'errors':re.search(r'ERROR SUMMARY: (.*)',m).group(1)})
  Path('docs/evidence/cp410-shell/official-profiles.json').write_text(json.dumps(rows,indent=2)+'\n')
print('COMPLETE exact traversal instructions/allocations across 1k/10k/100k')
