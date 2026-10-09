from pathlib import Path
import subprocess,json,tempfile,os,re
p=Path(__file__).resolve().parent/'data';
if os.name=='nt' or os.environ.get('MSYSTEM'):print('POSIX alias/path contracts; portable glob contracts run separately');raise SystemExit(0)
binaries=[Path(os.environ.get('NIFT','./nift')).resolve()];rows=[]
oracle=json.loads((p/'v410-glob-prefix.json').read_text());by_pattern={r['pattern']:r for r in oracle if 'pattern' in r};
patterns=[r'files/linked-dir/\../*.dat',r'files/**/\./*.dat',r'files/**/\../*.dat','files/*','files/**','files/**/**','files/**/**/**','files/**/*.dat','files/**/sub/*','files/*/*.dat','files/*/deep/?*.dat','files/**/sub/..','files/**/./*.dat','files/**/../*.dat','files/linked-dir/*.dat','files/linked-dir/**','files/**/deep/**','files/**/.*','files/.private/**','files/.private/*.dat','files/??.dat','files/?.dat','files/[ab]*.dat','files/**/[ab].dat','files/**/[a-z].dat','files/**/*.DAT','files/**/*.missing','files/**/no-such/*','files/linked-dir/../*.dat','files/dangling*','files/external*','files/link*','./files/**/*.dat','**/*.dat','**','**/**','../files/**/*.dat']
with tempfile.TemporaryDirectory(prefix='nift-prefix-general-') as directory:
 root=Path(directory)
 for name in ['files/a.dat','files/aa.dat','files/[ab].dat','files/é.dat','files/space name.dat','files/upper.DAT','files/.hidden.dat','files/.private/x.dat','files/sub/z.dat','files/sub/deep/b.dat','outside/t.dat','alternate/q.dat']:
  f=root/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b'x')
 for name,target in [('files/link.dat','sub/z.dat'),('files/external.dat','../outside/t.dat'),('files/linked-dir','sub'),('files/dangling.dat','absent')]: (root/name).symlink_to(target,target_is_directory=name.endswith('dir'))
 for pattern in patterns+[str(root/'files'/'**'/'*.dat'),str(root/'files'/'*')]:
  reference=by_pattern[pattern.replace(str(root),'<ORACLE_ROOT>')];expected=(reference['exit'],reference['stdout'],reference['stderr'])
  for binary in binaries:
   q=subprocess.run([str(binary),'-'],input='print(ls('+json.dumps(pattern,ensure_ascii=False)+').stringify())',cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60);actual=(q.returncode,q.stdout.replace(str(root),'<ORACLE_ROOT>'),q.stderr.replace(str(root),'<ORACLE_ROOT>'))
   assert actual==expected,(pattern,actual,expected)
  rows.append(dict(pattern=pattern.replace(str(root),'<ORACLE_ROOT>'),exit=expected[0],stdout=expected[1],stderr=expected[2]));print(pattern.replace(str(root),'<ORACLE_ROOT>'),'PASS',flush=True)
 # Retarget a directory alias between calls in the SAME Parser process.
 reference=next(row for row in oracle if row.get('name')=='same-parser-retarget')
 source=reference['source'];expected=(reference['exit'],reference['stdout'],reference['stderr'])
 link=root/'files/linked-dir';link.unlink();link.symlink_to('sub',target_is_directory=True)
 q=subprocess.run([str(binaries[0]),'-'],input=source,cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
 assert (q.returncode,q.stdout,q.stderr)==expected,(q.stdout,q.stderr,expected)
 assert link.readlink()==Path('../alternate'),'Retarget probe did not update alias'
 rows.append(reference);print('same-parser-retarget PASS',flush=True)
print('PASS',len(rows),'general/stable and between-operation alias contracts')

# Absolute glob calls preserve their otherwise-unused cwd observation.
with tempfile.TemporaryDirectory(prefix='nift-unlinked-cwd-') as directory:
 root=Path(directory);(root/'gone').mkdir();(root/'files').mkdir();(root/'files/a.dat').touch()
 source='result := run("rmdir",'+json.dumps(str(root/'gone'))+'); print(result.launched); print(result.exit_code); print(ls('+json.dumps(str(root/'files')+'/*.dat')+').stringify())'
 q=subprocess.run([str(binaries[0]),'-'],input=source,cwd=root/'gone',capture_output=True,text=True,encoding='utf-8',timeout=60)
 assert not (root/'gone').exists(),'Probe failed to remove cwd'
 assert q.returncode!=0 and q.stdout=='true\n0\n',(q.stdout,q.stderr)
 assert re.fullmatch(r'error: filesystem error: [^\n]*(?:current path|current_path)[^\n]*\n',q.stderr),q.stderr
print('PASS actual removed-cwd failure boundary')
