from pathlib import Path
import csv,hashlib,subprocess,tempfile,json,os
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
with (Path(__file__).resolve().parent/'data/v410-file-buffer-d172.tsv').open(newline='') as pins:
 pin_rows = list(csv.DictReader(pins,delimiter='\t'))
D172 = {pin['case']:pin for pin in pin_rows}
assert len(D172) == len(pin_rows) == 32
assert sorted(pin['kind'] for pin in pin_rows) == ['flow'] * 6 + ['nesting'] * 26
with (Path(__file__).resolve().parent/'data/v410-file-buffer-d173.tsv').open(newline='') as pins:
 pin_rows = list(csv.DictReader(pins,delimiter='\t'))
D173 = {pin['case']:pin for pin in pin_rows}
assert len(D173) == len(pin_rows) == 27
assert sorted(pin['kind'] for pin in pin_rows) == ['invalid-call'] * 4 + ['legacy-replace-depth'] * 2 + ['ownership'] * 7 + ['parser-limit'] * 8 + ['postfix'] * 6
assert not D172.keys() & D173.keys()
assert D172.keys() | D173.keys() <= {row['name'] for row in rows}
with (Path(__file__).resolve().parent/'data/v410-file-buffer-d174.tsv').open(newline='') as pins:
 pin_rows = list(csv.DictReader(pins,delimiter='\t'))
D174 = {pin['case']:pin for pin in pin_rows}
assert len(D174) == len(pin_rows) == 5
assert all(pin['kind'] == 'nested-replace' for pin in pin_rows)
assert D174.keys() <= {row['name'] for row in rows}
assert not D174.keys() & (D172.keys() | D173.keys())
REFUSALS = D172 | D173 | D174
INITIAL_FILES = {name:b'ABCDEFGH'.hex() for name in ('data','sub/data','sub/sub/data')}

def check_refusal(actual, expected, diagnostic):
 assert actual['exit'] == 1 and actual['stdout'] == '' and actual['files'] == expected['files'], actual
 assert actual['stderr'].splitlines()[0] == diagnostic, actual
 assert 'note: the program was not executed: no part of it ran' in actual['stderr'], actual

def run_case(root,path,mode):
 q = subprocess.run([str(binary),native_path(path)],cwd=root,env=dict(os.environ,NIFT_VM=mode),
                    capture_output=True,text=True,encoding='utf-8',timeout=60)
 return dict(exit=q.returncode,stdout=normalize(q.stdout,root),stderr=normalize(q.stderr,root),
             files={f.relative_to(root).as_posix():f.read_bytes().hex()
                    for f in root.rglob('*') if f.is_file() and f!=path})

def check_depth_boundaries():
 for token,base,last,output in [('id(','f.exists()',29,'true'),('type(','"x"',30,'string'),
                                ('html_escape(','"x"',30,'x'),('min(','1',30,'1')]:
  with tempfile.TemporaryDirectory(prefix='nift-file-depth-') as directory:
   root=Path(directory).resolve()
   path=root/'case.f'
   (root/'data').touch()
   for depth in (last,last+1):
    path.write_text('f:=file("data");fn(id(x)){return x};print('+token*depth+base+')'*depth+')\n')
    actual=run_case(root,path,'require')
    if depth==last:
     assert (actual['exit'],actual['stdout'],actual['stderr']) == (0,output+'\n',''), actual
    else:
     diagnostic=('error: <ORACLE_ROOT>/case.f:1:37: not compiled for the VM (opaque:nesting-limit): '
                 'source the VM parser does not structure (nesting-limit) is not supported by the Runtime 2.0 VM yet')
     check_refusal(actual, {'files':{'data':''}}, diagnostic)
   print(f'PASS VM depth boundary {token} {last}/{last+1}',flush=True)

def check_adjacent_postfix():
 with tempfile.TemporaryDirectory(prefix='nift-file-postfix-') as directory:
  root=Path(directory).resolve()
  path=root/'case.f'
  path.write_text('print(" x ".trim().trim())\n')
  actual=run_case(root,path,'require')
  assert actual == dict(exit=0,stdout='x\n',stderr='',files={}), actual
  print('PASS compact nested trim control',flush=True)

def check_replace_boundaries():
 with tempfile.TemporaryDirectory(prefix='nift-file-replace-') as directory:
  root=Path(directory).resolve()
  path=root/'case.f'
  expected=dict(exit=0,stdout='y\n',stderr='',files={})
  path.write_text('r:="x".replace("x","y");print("x".replace("x",r))\n')
  for engine in ('0','require'):
   assert run_case(root,path,engine) == expected
  for depth in (1,2,20,21):
   path.write_text('f:=file("data");fn(id(x)){return x};print('+'"x".replace("x",'*depth+'"y"'+')'*(depth+1)+'\n')
   assert run_case(root,path,'0') == expected
   result=run_case(root,path,'require')
   if depth == 1:
    assert result == expected, result
   else:
    diagnostic = (
     'error: <ORACLE_ROOT>/case.f:1:59: not compiled for the VM (builtin-lazy-args:replace): '
     'lazily evaluated arguments of `replace()` is not supported by the Runtime 2.0 VM yet'
     if depth < 21 else D174['stack-method-argument-40']['diagnostic'])
    check_refusal(result,expected,diagnostic)
  print('PASS nested replace boundaries and precomputed replacement',flush=True)

def check_row(row):
 with tempfile.TemporaryDirectory(prefix='nift-file-buffer-oracle-') as directory:
  root=Path(directory).resolve();(root/'sub/sub').mkdir(parents=True)
  for name in ['data','sub/data','sub/sub/data']:(root/name).write_bytes(b'ABCDEFGH')
  path=root/'case.f';path.write_text(row['source']+'\n')
  mode = os.environ.get('NIFT_VM','require')
  actual=run_case(root,path,mode)
  expected={key:row[key] for key in actual}
  if mode != '0' and (row['name'] == WRITE_BYTES_REFUSAL or row['name'] in REFUSALS):
   if row['name'] == WRITE_BYTES_REFUSAL:
    diagnostic=actual['stderr'].splitlines()[0]
    assert diagnostic.startswith('error: <ORACLE_ROOT>/case.f:1:30: not compiled for the VM (host-handle-flow): '), actual
   else:
    pin=REFUSALS[row['name']]
    assert hashlib.sha256(row['source'].encode()).hexdigest() == pin['source_sha256'], row['name']
    diagnostic=pin['diagnostic']
    assert expected['exit'] == (0 if pin['kind'] in ('flow','nesting','postfix','nested-replace') else 1), expected
    if pin['kind'] in ('parser-limit','legacy-replace-depth','invalid-call'):
     assert expected['stdout'] == '' and expected['files'] == INITIAL_FILES, expected
    if pin['kind'] == 'parser-limit':
     assert 'expression nesting exceeds parser limit' in expected['stderr'], expected
   # The oracle records the final legacy files, which may differ from setup.
   # Refusal must leave every fixture at its initial bytes.
   untouched={'files':INITIAL_FILES}
   check_refusal(actual,untouched,diagnostic)
   line=diagnostic.replace('/case.f:1:','/case.f:2:',1)
   path.write_text('print("REFUSAL_EXECUTION_SENTINEL")\n'+row['source']+'\n')
   check_refusal(run_case(root,path,mode),untouched,line)
   path.write_text(row['source']+'\n')
   legacy=run_case(root,path,'0')
   assert legacy == expected, (row['name'],legacy,expected)
   print('PASS',row['name'],'(approved VM pre-execution refusal; exact legacy execution)',flush=True)
   return 'refusal'
  if os.environ.get('NIFT_VM')!='0' and row['name'] in APPROVED_STDERR:
   assert row['stderr'].endswith(': script failed\n'),(row['name'],'the approval is for legacy\'s dropped message only')
   expected['stderr']=APPROVED_STDERR[row['name']]
  if mode != '0' and row['name'] == 'duplicate-replace-once-error':
   cleanup = 'managed file left open: <ORACLE_ROOT>/data (unsaved changes discarded)\n'
   assert expected == dict(exit=1,stdout='',stderr='error: '+cleanup,files=INITIAL_FILES), expected
   assert run_case(root,path,'0') == expected, expected
   expected['stderr'] = 'error: <ORACLE_ROOT>/case.f:1:1: '+cleanup
  assert actual==expected,(row['name'],actual,expected)
  print('PASS',row['name'],flush=True)
  return 'execution'
def check_append_controls():
 with tempfile.TemporaryDirectory(prefix='nift-file-append-') as directory:
  root = Path(directory).resolve()
  data = root/'data'
  script = root/'case.f'
  def run(source, original, budget=None):
   data.write_bytes(original)
   script.write_text(source+'\n')
   env = dict(os.environ,NIFT_VM='require')
   env.pop('NIFT_VM_HEAP_BUDGET',None)
   if budget is not None:
    env['NIFT_VM_HEAP_BUDGET'] = str(budget)
   result = subprocess.run([str(binary),native_path(script)],cwd=root,env=env,
                           capture_output=True,text=True,timeout=60)
   return result,data.read_bytes()
  result, actual = run('f:=file("data");f.open("rw");f.seek(2);f.append("xy");print(f.tell());f.save();f.close()',b'ABCDEFGH')
  assert (result.returncode,result.stdout,result.stderr,actual) == (0,'2\n','',b'ABCDEFGHxy'), (result,actual)
  result, actual = run('f:=file("data");f.open("r");f.append("xy")',b'ABCDEFGH')
  assert result.returncode == 1 and 'append: file is not open for writing' in result.stderr and actual == b'ABCDEFGH', (result,actual)
  # Force allocation beyond any small-string capacity. The old two-byte
  # append could fit in spare capacity after a read on libc++.
  original = b'ABCDEFGH'
  payload = 'z' * 65536
  source = 'f:=file("data");f.open("rw");payload:="'+payload+'";print("ready");f.append(payload);f.save();f.close()'
  result, actual = run(source,original,100000)
  assert result.returncode == 70 and result.stdout == 'ready\n' and 'memory budget exceeded' in result.stderr and actual == original, result
  result, actual = run(source,original,1000000)
  assert (result.returncode,result.stdout,result.stderr,actual) == (0,'ready\n','',original+payload.encode()), result
  print('PASS append controls: exact bytes, unchanged cursor, read-only error and allocation stop',flush=True)

if os.environ.get('NIFT_VM') != '0':
 check_append_controls()
 check_depth_boundaries()
 check_adjacent_postfix()
 check_replace_boundaries()
failures = refusals = 0
for row in rows:
 try:
  refusals += check_row(row) == 'refusal'
 except AssertionError as error:
  print('FAIL',row['name'],error,flush=True)
  failures += 1
print(f'file-buffer oracle: {len(rows)-failures-refusals} execution matches, {refusals} approved refusals, {failures} failures ({len(rows)} rows)',flush=True)
raise SystemExit(bool(failures))
