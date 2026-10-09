from pathlib import Path
import subprocess,json,tempfile
p=Path('.build/cp410-implementation/traversal').resolve();binaries=[Path('.build/cp410-implementation/snapshot-nift').resolve(),p/'nift'];rows=[]
patterns=[r'files/linked-dir/\../*.dat',r'files/**/\./*.dat',r'files/**/\../*.dat','files/*','files/**','files/**/**','files/**/**/**','files/**/*.dat','files/**/sub/*','files/*/*.dat','files/*/deep/?*.dat','files/**/sub/..','files/**/./*.dat','files/**/../*.dat','files/linked-dir/*.dat','files/linked-dir/**','files/**/deep/**','files/**/.*','files/.private/**','files/.private/*.dat','files/??.dat','files/?.dat','files/[ab]*.dat','files/**/[ab].dat','files/**/[a-z].dat','files/**/*.DAT','files/**/*.missing','files/**/no-such/*','files/linked-dir/../*.dat','files/dangling*','files/external*','files/link*','./files/**/*.dat','**/*.dat','**','**/**','../files/**/*.dat']
with tempfile.TemporaryDirectory(prefix='nift-prefix-general-') as directory:
 root=Path(directory)
 for name in ['files/a.dat','files/aa.dat','files/[ab].dat','files/é.dat','files/space name.dat','files/upper.DAT','files/.hidden.dat','files/.private/x.dat','files/sub/z.dat','files/sub/deep/b.dat','outside/t.dat','alternate/q.dat']:
  f=root/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b'x')
 for name,target in [('files/link.dat','sub/z.dat'),('files/external.dat','../outside/t.dat'),('files/linked-dir','sub'),('files/dangling.dat','absent')]: (root/name).symlink_to(target,target_is_directory=name.endswith('dir'))
 for pattern in patterns+[str(root/'files'/'**'/'*.dat'),str(root/'files'/'*')]:
  expected=None
  for binary in binaries:
   q=subprocess.run([str(binary),'-'],input='print(ls('+json.dumps(pattern,ensure_ascii=False)+').stringify())',cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60);actual=(q.returncode,q.stdout.replace(str(root),'<ORACLE_ROOT>'),q.stderr.replace(str(root),'<ORACLE_ROOT>'))
   if expected is None:expected=actual
   assert actual==expected,(pattern,actual,expected)
  rows.append(dict(pattern=pattern.replace(str(root),'<ORACLE_ROOT>'),exit=expected[0],stdout=expected[1],stderr=expected[2]));print(pattern.replace(str(root),'<ORACLE_ROOT>'),'PASS',flush=True)
 # Retarget a directory alias between calls in the SAME Parser process.
 source='print(ls("files/linked-dir/*.dat").stringify()); result := run("ln","-sfn","../alternate","files/linked-dir"); print(result.launched); print(result.exit_code); print(ls("files/linked-dir/*.dat").stringify())'
 expected=None
 for binary in binaries:
  link=root/'files/linked-dir';link.unlink();link.symlink_to('sub',target_is_directory=True)
  q=subprocess.run([str(binary),'-'],input=source,cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60);actual=(q.returncode,q.stdout,q.stderr)
  if expected is None:expected=actual
  assert q.returncode==0 and q.stdout=='[\"files/sub/z.dat\"]\ntrue\n0\n[\"alternate/q.dat\"]\n',(q.stdout,q.stderr)
  assert actual==expected,(actual,expected)
 rows.append(dict(name='same-parser-retarget',source=source,exit=expected[0],stdout=expected[1],stderr=expected[2]));print('same-parser-retarget PASS',flush=True)
(p/'contracts.json').write_text(json.dumps(rows,indent=2)+'\n');print('PASS',len(rows),'general/stable and between-operation alias contracts')
