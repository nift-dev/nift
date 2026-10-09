from pathlib import Path
import json,subprocess,shutil
p=Path('.build/cp410-shell').resolve();payload=('0123456789abcdef'*7+'0123456789abcde\n').encode();rows=[]
subprocess.run(['g++','-std=c++17','-O2',str(p/'secondary-control.cpp'),'-o',str(p/'secondary-control')],check=True)
for op in ('metadata','move','copy','concat','string'):
 for label in ('baseline','native'):
  for tool in ('callgrind','memcheck','strace'):
   root=p/f'secondary-profile-{op}-{label}-{tool}';shutil.rmtree(root,ignore_errors=True);root.mkdir()
   for d in ('files','copied','moved'):(root/d).mkdir()
   names=[f'files/obj-{i:06}-target.dat' for i in range(1000)]
   for name in names:(root/name).write_bytes(payload)
   (root/'targets.txt').write_text('\n'.join(names)+'\n')
   code=(p/f'{op}-1000'/'source.f').read_text();cmd=[str(p/'baseline-nift'),'-e',code] if label=='baseline' else [str(p/'secondary-control'),op];stem=f'secondary-{op}-{label}'
   if tool=='strace':args=['strace','-c','-o',str(p/(stem+'.strace'))]
   else:
    args=['valgrind','--tool='+tool,'--log-file='+str(p/(stem+'-'+tool+'.log'))];args+=['--callgrind-out-file='+str(p/(stem+'.callgrind'))] if tool=='callgrind' else ['--leak-check=full','--error-exitcode=99']
   r=subprocess.run([*args,*cmd],cwd=root,capture_output=True,text=True,check=True);assert r.stdout.strip()==('128000' if op=='metadata' else 'aLPHa' if op=='string' else 'OK')
   if op=='concat':assert (root/'output').read_bytes()==payload*1000
   print('PASS',op,label,tool,flush=True)
print('COMPLETE secondary kernel/instruction/allocation controls')
