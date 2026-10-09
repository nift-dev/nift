from pathlib import Path
import subprocess,json,os
p=Path('.build/cp410-shell').resolve();root=p/'semantic-tree';root.mkdir(exist_ok=True)
for f in ['files/a.dat','files/.hidden.dat','files/.hidden/x.dat','files/sub/z.dat','files/sub/é.dat','files/sub/space name.dat','files/sub/deep/b.dat','files/literal*.dat','outside/t.dat']:
 q=root/f;q.parent.mkdir(parents=True,exist_ok=True);q.write_text('x')
for name,target in [('files/link.dat','sub/z.dat'),('files/dangling.dat','absent'),('files/linked-dir','sub'),('files/external.dat','../outside/t.dat')]:
 q=root/name
 if not q.is_symlink():q.symlink_to(target,target_is_directory=name.endswith('dir'))
patterns=['files/**/*.dat','files/**/**/*.dat','files/*','files/.hidden/*.dat','files/**/.*','files/literal\\*.dat','files/**/*.missing',str(root/'files/**/*.dat'),'files/linked-dir/*.dat','files/external*.dat']
rows=[]
for pattern in patterns:
 code='print(ls('+json.dumps(pattern)+').join("\\n"))';r=[]
 for label in ('baseline','prototype'):
  x=subprocess.run([str(p/(label+'-nift')),'-e',code],cwd=root,capture_output=True,text=True);r.append((x.returncode,x.stdout,x.stderr))
 assert r[0][0]==(1 if pattern=='files/literal\\*.dat' else 0),(pattern,r)
 assert r[0]==r[1],(pattern,r)
 rows.append({'pattern':pattern,'returncode':r[0][0],'stdout':r[0][1],'stderr':r[0][2]})
Path('docs/evidence/cp410-shell/traversal-semantics.json').write_text(json.dumps(rows,indent=2,ensure_ascii=False)+'\n');print('PASS',len(rows),'exact traversal output/error comparisons')
