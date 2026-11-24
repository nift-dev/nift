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
# The six historical diagnostics below depend on legacy parser or factory
# evaluation. Keep their byte-for-byte witnesses in the historical engine.
# The compatible rows still exercise the VM.
legacy_diagnostics = {
 'shape-.to_upper().replace("A","x",)': (23, 'opaque:legacy-runtime-error'),
 'shape-.to_upper().replace(,"A","x")': (23, 'opaque:legacy-runtime-error'),
 'shape-.to_upper(,).replace("A","x")': (23, 'opaque:legacy-runtime-error'),
 'receiver-factory': (10052, 'type:length'),
 'argument-factory': (53, 'type:length'),
 'nested-error': (10047, 'unresolved-name'),
}
assert legacy_diagnostics.keys() <= {row['name'] for row in rows}
with tempfile.TemporaryDirectory(prefix='nift-large-string-oracle-') as directory:
 root=Path(directory).resolve()
 for row in rows:
  file=root/'case.f';file.write_text(row['source']+'\n',encoding='utf-8')
  modes = ('0',) if row['name'] in legacy_diagnostics else ('0','require')
  for mode in modes:
   q=subprocess.run([str(binary),native_path(file)],cwd=root,env=dict(os.environ,NIFT_VM=mode),
                    text=True,encoding='utf-8',capture_output=True,timeout=60)
   actual=(q.returncode,q.stdout,normalize(q.stderr,root))
   assert actual==(row['exit'],row['stdout'],row['stderr']),(row['name'],mode,actual)
  if row['name'] in legacy_diagnostics:
   column, reason = legacy_diagnostics[row['name']]
   env = dict(os.environ)
   env.pop('NIFT_VM', None)
   for line, source in ((1, row['source']), (2, 'print("REFUSAL_EXECUTION_SENTINEL")\n'+row['source'])):
    file.write_text(source+'\n', encoding='utf-8')
    q = subprocess.run([str(binary),native_path(file)],cwd=root,env=env,
                       text=True,encoding='utf-8',capture_output=True,timeout=60)
    diagnostic = normalize(q.stderr,root)
    prefix = f'error: <ORACLE_ROOT>/case.f:{line}:{column}: not compiled for the VM ({reason}): '
    assert q.returncode == 1 and q.stdout == '', (row['name'],q)
    assert diagnostic.startswith(prefix) and 'note: the program was not executed: no part of it ran' in diagnostic, (row['name'],diagnostic)
    assert set(root.iterdir()) == {file}, row['name']
   print('PASS',row['name'],'(exact legacy diagnostic and VM-default refusal control)')
  else:
   print('PASS',row['name'])
print('PASS',len(rows),'historical contracts,',len(rows)-len(legacy_diagnostics),'VM matches and',len(legacy_diagnostics),'VM refusal controls')
