from pathlib import Path
import subprocess,tempfile,json,os
def native_path(path):
 if os.environ.get('MSYSTEM'):
  return subprocess.check_output(['cygpath','-m',str(path)],text=True).strip()
 return path.as_posix()
def normalize(value,root):
 # Normalize only this fixture's exact physical/native root, longest first.
 # MSYS Python uses POSIX paths while the native CLI reports Windows paths.
 variants={str(root),root.as_posix(),native_path(root)}
 variants|={v.replace('/','\\') for v in tuple(variants)}
 for prefix in sorted(variants,key=len,reverse=True):value=value.replace(prefix,'<ORACLE_ROOT>')
 return value
import gzip
binary=Path(os.environ.get('NIFT','./nift')).resolve();rows=json.loads(gzip.decompress((Path(__file__).resolve().parent/'data/v410-file-buffer-oracle.json.gz').read_bytes()))
for row in rows:
 with tempfile.TemporaryDirectory(prefix='nift-file-buffer-oracle-') as directory:
  root=Path(directory).resolve();(root/'sub/sub').mkdir(parents=True)
  for name in ['data','sub/data','sub/sub/data']:(root/name).write_bytes(b'ABCDEFGH')
  path=root/'case.f';path.write_text(row['source']+'\n')
  q=subprocess.run([str(binary),native_path(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  actual=dict(exit=q.returncode,stdout=normalize(q.stdout,root),stderr=normalize(q.stderr,root),files={f.relative_to(root).as_posix():f.read_bytes().hex() for f in root.rglob('*') if f.is_file() and f.name!='case.f'})
  expected={key:row[key] for key in actual};assert actual==expected,(row['name'],actual,expected)
  print('PASS',row['name'],flush=True)
print('PASS',len(rows),'exact buffer/factory/cwd/reentry/file-effect contracts')
