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
p=Path(__file__).resolve().parent/'data';binary=Path(os.environ.get('NIFT','./nift')).resolve()
with gzip.open(p/'v410-large-string-oracle.json.gz','rt',encoding='utf-8') as f:rows=json.load(f)
with tempfile.TemporaryDirectory(prefix='nift-large-string-oracle-') as directory:
 root=Path(directory).resolve()
 for row in rows:
  file=root/'case.f';file.write_text(row['source']+'\n',encoding='utf-8')
  q=subprocess.run([str(binary),native_path(file)],cwd=root,text=True,encoding='utf-8',capture_output=True,timeout=60)
  actual=(q.returncode,q.stdout,normalize(q.stderr,root))
  assert actual==(row['exit'],row['stdout'],row['stderr']),(row['name'],actual)
  print('PASS',row['name'])
print('PASS',len(rows),'exact payload/factory/diagnostic contracts')
