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
# Owner 2026-11-10 (docs/runtime-2/compatibility.md, "loop write diagnostics"): when write or
# write_line fails inside a while/for body, legacy's loop executor drops the method's message and
# reports "script failed"; the VM reports the method's own error at the same position. Approved
# for exactly these rows and only for the stderr text: exit status, stdout and file bytes must
# still equal the oracle.
APPROVED_STDERR={
 'closed-write-factory-count':'error: <ORACLE_ROOT>/case.f:1:73: write: file is not open\n',
 'readonly-write-factory-count':'error: <ORACLE_ROOT>/case.f:1:85: write: file is not open for writing\n',
}
# D-166 approves one case: the historical API spelling already errors before
# mutation. Keep the complete legacy oracle and require the VM to refuse it
# before execution, with the same empty output and unchanged files.
WRITE_BYTES_REFUSAL = 'mutate-write_bytes(bytes([0,255,65]))'
for row in rows:
 with tempfile.TemporaryDirectory(prefix='nift-file-buffer-oracle-') as directory:
  root=Path(directory).resolve();(root/'sub/sub').mkdir(parents=True)
  for name in ['data','sub/data','sub/sub/data']:(root/name).write_bytes(b'ABCDEFGH')
  path=root/'case.f';path.write_text(row['source']+'\n')
  q=subprocess.run([str(binary),native_path(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  actual=dict(exit=q.returncode,stdout=normalize(q.stdout,root),stderr=normalize(q.stderr,root),files={f.relative_to(root).as_posix():f.read_bytes().hex() for f in root.rglob('*') if f.is_file() and f.name!='case.f'})
  expected={key:row[key] for key in actual}
  if os.environ.get('NIFT_VM') != '0' and row['name'] == WRITE_BYTES_REFUSAL:
   assert actual['exit'] == expected['exit'] == 1, (row['name'],actual)
   diagnostic = actual['stderr'].splitlines()[0]
   assert diagnostic.startswith('error: <ORACLE_ROOT>/case.f:1:30: not compiled for the VM (host-handle-flow): '), (row['name'],actual)
   assert 'note: the program was not executed: no part of it ran' in actual['stderr'], (row['name'],actual)
   assert actual['stdout'] == expected['stdout'] == '' and actual['files'] == expected['files'], (row['name'],actual)
   legacy=subprocess.run([str(binary),native_path(path)],cwd=root,env=dict(os.environ,NIFT_VM='0'),
                         capture_output=True,text=True,encoding='utf-8',timeout=60)
   legacy_actual=dict(exit=legacy.returncode,stdout=normalize(legacy.stdout,root),
                      stderr=normalize(legacy.stderr,root),
                      files={f.relative_to(root).as_posix():f.read_bytes().hex()
                             for f in root.rglob('*') if f.is_file() and f.name!='case.f'})
   assert legacy_actual == expected, (row['name'],legacy_actual,expected)
   print('PASS',row['name'],'(VM pre-execution refusal; legacy error and file bytes exact)',flush=True)
   continue
  if os.environ.get('NIFT_VM')!='0' and row['name'] in APPROVED_STDERR:
   assert row['stderr'].endswith(': script failed\n'),(row['name'],'the approval is for legacy\'s dropped message only')
   expected['stderr']=APPROVED_STDERR[row['name']]
  assert actual==expected,(row['name'],actual,expected)
  print('PASS',row['name'],flush=True)
print('PASS',len(rows),'exact buffer/factory/cwd/reentry/file-effect contracts')
