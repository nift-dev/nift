from pathlib import Path
import subprocess,json
p=Path('.build/cp410-shell').resolve();root=p/'flat-1000';code='paths := ls("files/**/*.dat"); print(paths.size())'
subprocess.run(['g++','-std=c++17','-O2',str(p/'native.cpp'),'-o',str(p/'native')],check=True)
for label in ('baseline','prototype','native'):
 cmd=[str(p/'native')] if label=='native' else [str(p/(label+'-nift')),'-e',code]
 for tool in ('callgrind','memcheck'):
  args=['valgrind','--tool='+tool,'--log-file='+str(p/f'{label}-{tool}.log')]
  if tool=='callgrind':args+=['--callgrind-out-file='+str(p/(label+'.callgrind'))]
  else:args+=['--leak-check=full','--error-exitcode=99']
  subprocess.run([*args,*cmd],cwd=root,check=True,stdout=subprocess.DEVNULL)
print('PASS three local traversal controls under Callgrind and Memcheck')
