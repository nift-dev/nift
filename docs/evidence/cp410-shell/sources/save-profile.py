from pathlib import Path
import json,subprocess
p=Path('.build/cp410-shell').resolve();payload='0123456789abcdef'*7+'0123456789abcde\n'
code='paths := open("targets.txt").trim().split("\\n"); for(p : paths) { f := file(p); f.open("w"); f.write('+json.dumps(payload)+'); f.save(); f.close() }; print("OK")'
for tool in ('callgrind','memcheck','strace'):
 root=p/('save-profile-'+tool);(root/'files').mkdir(parents=True,exist_ok=True);names=[f'files/out-{i}' for i in range(1000)]
 for name in names:
  q=root/name
  if q.exists():q.unlink()
 (root/'targets.txt').write_text('\n'.join(names)+'\n');cmd=[str(p/'baseline-nift'),'-e',code]
 if tool=='strace':args=['strace','-c','-o',str(p/'save-1000.strace')]
 else:
  args=['valgrind','--tool='+tool,'--log-file='+str(p/('save-'+tool+'.log'))]
  args+=['--callgrind-out-file='+str(p/'save.callgrind')] if tool=='callgrind' else ['--leak-check=full','--error-exitcode=99']
 subprocess.run([*args,*cmd],cwd=root,check=True)
 assert all((root/name).read_bytes()==payload.encode() for name in names)
 assert len(list((root/'files').iterdir()))==1000
print('PASS exact 1k fresh atomic saves under three profilers')
