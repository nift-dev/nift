from pathlib import Path
import subprocess,tempfile,json
p=Path(__file__).parent.resolve();wave=Path('.build/cp410-wave3').resolve();rows=[]
for shape in ['mixed-aliases','deep','wide']:
 with tempfile.TemporaryDirectory() as directory:
  root=Path(directory);names=['files/a.dat','files/.hidden.dat','files/.private/x.dat','files/sub/z.dat','files/sub/é.dat','files/sub/space name.dat','files/sub/deep/b.dat','outside/t.dat']
  if shape=='deep':names += ['files/'+('/'.join('d'+str(i) for i in range(n)))+'/x.dat' for n in range(1,35)]
  if shape=='wide':names += [f'files/dir-{i//100}/value-{i}.dat' for i in range(4000)]
  for name in names:
   f=root/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b'x')
  for name,target in [('files/link.dat','sub/z.dat'),('files/dangling.dat','absent'),('files/linked-dir','sub'),('files/external.dat','../outside/t.dat')]:
   (root/name).symlink_to(target,target_is_directory=name.endswith('dir'))
  n=subprocess.run([str(wave/'string-nift'),'-'],input='print(ls("files/**/*.dat").stringify())',cwd=root,capture_output=True,text=True,check=True)
  expected=json.loads(n.stdout);c=subprocess.run([str(p/'native-prefix-cache'),'--show'],cwd=root,capture_output=True,text=True,check=True)
  assert c.stdout.splitlines()==expected,(shape,c.stdout,expected)
  rows.append(dict(shape=shape,matched=len(expected),ordered_output_equal=True,scope='fixed suffix under stable filesystem; hidden entries, symlink leaves/aliases/dangling and non-followed directory symlinks'))
(p/'prefix-cache-contracts.json').write_text(json.dumps(rows,indent=2)+'\n');print('PASS',len(rows),'stable fixtures')
