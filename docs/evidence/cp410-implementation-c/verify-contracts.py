from pathlib import Path
import subprocess,tempfile,json
p=Path('.build/cp410-implementation/filesystem').resolve();binary=p/'nift';rows=json.loads((p/'contracts.json').read_text())
for row in rows:
 with tempfile.TemporaryDirectory(prefix='nift-filesystem-oracle-') as directory:
  root=Path(directory);(root/'one').mkdir();(root/'two').mkdir()
  for file,data in [('a','A'),('b','B'),('one/a','ONE'),('two/a','TWO')]: (root/file).write_text(data)
  source=row['source'].replace('<ORACLE_ROOT>',str(root));path=root/'case.f';path.write_text(source+'\n')
  q=subprocess.run([str(binary),str(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  files={str(f.relative_to(root)):f.read_text() for f in root.rglob('*') if f.is_file() and f.name!='case.f'}
  actual=dict(exit=q.returncode,stdout=q.stdout.replace(str(root),'<ORACLE_ROOT>'),stderr=q.stderr.replace(str(root),'<ORACLE_ROOT>'),files=files);expected={k:row[k] for k in actual};assert actual==expected,(row['name'],actual,expected)
  print('PASS',row['name'],flush=True)
print('PASS all',len(rows),'exact source-count/order/origin/error/file-effect contracts')
