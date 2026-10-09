from pathlib import Path
import tempfile,subprocess,json
binaries=[Path('.build/cp410-implementation/snapshot-nift').resolve(),Path('.build/cp410-implementation/traversal/nift').resolve()];rows=[]
for binary in binaries:
 with tempfile.TemporaryDirectory(prefix='nift-unlinked-cwd-') as directory:
  root=Path(directory);(root/'gone').mkdir();(root/'files').mkdir();(root/'files/a.dat').touch();source='result := run("rmdir",'+json.dumps(str(root/'gone'))+'); print(result.launched); print(result.exit_code); print(ls('+json.dumps(str(root/'files')+'/*.dat')+').stringify())'
  q=subprocess.run([str(binary),'-'],input=source,cwd=root/'gone',capture_output=True,text=True,encoding='utf-8',timeout=60)
  assert not (root/'gone').exists(),'Probe failed to remove cwd'
  assert q.returncode!=0 and q.stdout=='true\n0\n',(q.stdout,q.stderr)
  rows.append(dict(binary=str(binary),exit=q.returncode,stdout=q.stdout.replace(str(root),'<ORACLE_ROOT>'),stderr=q.stderr.replace(str(root),'<ORACLE_ROOT>')))
assert (rows[0]['exit'],rows[0]['stdout'],rows[0]['stderr'])==(rows[1]['exit'],rows[1]['stdout'],rows[1]['stderr']),rows
Path('.build/cp410-implementation/traversal/unlinked-cwd.json').write_text(json.dumps(rows,indent=2)+'\n');print('PASS actual removed-cwd failure boundary',rows[0]['stderr'].strip())
