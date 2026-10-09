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
p=Path(__file__).resolve().parent/'data';binary=Path(os.environ.get('NIFT','./nift')).resolve();rows=json.loads((p/'v410-filesystem-contracts.json').read_text())
for row in rows:
 with tempfile.TemporaryDirectory(prefix='nift-filesystem-oracle-') as directory:
  root=Path(directory).resolve();(root/'one').mkdir();(root/'two').mkdir()
  for file,data in [('a','A'),('b','B'),('one/a','ONE'),('two/a','TWO')]: (root/file).write_text(data)
  source=row['source'].replace('<ORACLE_ROOT>',native_path(root));path=root/'case.f';path.write_text(source+'\n')
  q=subprocess.run([str(binary),native_path(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  files={f.relative_to(root).as_posix():f.read_text() for f in root.rglob('*') if f.is_file() and f.name!='case.f'}
  actual=dict(exit=q.returncode,stdout=normalize(q.stdout,root),stderr=normalize(q.stderr,root),files=files);expected={k:row[k] for k in actual};assert actual==expected,(row['name'],actual,expected)
  print('PASS',row['name'],flush=True)
print('PASS all',len(rows),'exact source-count/order/origin/error/file-effect contracts')

rows=json.loads((p/'v410-filesystem-pure-contracts.json').read_text())
with tempfile.TemporaryDirectory(prefix='nift-pure-oracle-') as directory:
 root=Path(directory).resolve()
 for i,row in enumerate(rows):
  path=root/'case.f';path.write_text(row['source']+'\n')
  q=subprocess.run([str(binary),native_path(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  actual=(q.returncode,q.stdout,normalize(q.stderr,root))
  assert actual==(row['exit'],row['stdout'],row['stderr']),(i,actual,row)
print('PASS',len(rows),'pure-string canonical fallback contracts')
