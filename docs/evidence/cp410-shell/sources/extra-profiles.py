from pathlib import Path
import subprocess,json
p=Path('.build/cp410-shell').resolve();root=p/'flat-1000';code='paths := ls("files/**/*.dat"); print(paths.size())'
for tool in ('callgrind','memcheck'):
 args=['valgrind','--tool='+tool,'--log-file='+str(p/f'unsorted-{tool}.log')]
 args+=['--callgrind-out-file='+str(p/'unsorted.callgrind')] if tool=='callgrind' else ['--leak-check=full','--error-exitcode=99']
 subprocess.run([*args,str(p/'unsorted-nift'),'-e',code],cwd=root,check=True,stdout=subprocess.DEVNULL)
root=p/'semantic-tree';a=subprocess.check_output([str(p/'baseline-nift'),'-e','print(ls("files/**/*.dat").join("\\n"))'],cwd=root);b=subprocess.check_output([str(p/'native'),'--list'],cwd=root);assert a==b,(a,b)
print('PASS native ordered symlink output and unsorted-prototype memory/instruction profiles')
