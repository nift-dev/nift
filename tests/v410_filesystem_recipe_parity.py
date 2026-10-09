from pathlib import Path
import subprocess,tempfile,json,os
p=Path(__file__).resolve().parent/'data';binary=Path(os.environ.get('NIFT','./nift')).resolve();rows=json.loads((p/'v410-filesystem-contracts.json').read_text())
for row in rows:
 with tempfile.TemporaryDirectory(prefix='nift-filesystem-oracle-') as directory:
  root=Path(directory);(root/'one').mkdir();(root/'two').mkdir()
  for file,data in [('a','A'),('b','B'),('one/a','ONE'),('two/a','TWO')]: (root/file).write_text(data)
  source=row['source'].replace('<ORACLE_ROOT>',root.as_posix());path=root/'case.f';path.write_text(source+'\n')
  q=subprocess.run([str(binary),str(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  files={f.relative_to(root).as_posix():f.read_text() for f in root.rglob('*') if f.is_file() and f.name!='case.f'}
  actual=dict(exit=q.returncode,stdout=q.stdout.replace(str(root),'<ORACLE_ROOT>').replace(root.as_posix(),'<ORACLE_ROOT>'),stderr=q.stderr.replace(str(root),'<ORACLE_ROOT>').replace(root.as_posix(),'<ORACLE_ROOT>'),files=files);expected={k:row[k] for k in actual};assert actual==expected,(row['name'],actual,expected)
  print('PASS',row['name'],flush=True)
print('PASS all',len(rows),'exact source-count/order/origin/error/file-effect contracts')

rows=json.loads((p/'v410-filesystem-pure-contracts.json').read_text())
with tempfile.TemporaryDirectory(prefix='nift-pure-oracle-') as directory:
 root=Path(directory)
 for i,row in enumerate(rows):
  path=root/'case.f';path.write_text(row['source']+'\n')
  q=subprocess.run([str(binary),str(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  actual=(q.returncode,q.stdout,q.stderr.replace(str(root),'<ORACLE_ROOT>').replace(root.as_posix(),'<ORACLE_ROOT>'))
  assert actual==(row['exit'],row['stdout'],row['stderr']),(i,actual,row)
print('PASS',len(rows),'pure-string canonical fallback contracts')
