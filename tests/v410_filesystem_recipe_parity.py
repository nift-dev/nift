from pathlib import Path
import subprocess,tempfile,json,os,sys,hashlib,re
sys.path.insert(0,str(Path(__file__).resolve().parent/'runtime2'))
import approved_refusal
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
# The VM-default refusal expected per case (review of C83-C99, P2-1, and of C100-C102, P1-1; combined-b3-b5.md
# sections 89 and 91). A refusal counts only when it is the case's pinned reason and that reason's inventory status
# is approved or deferred-named; every other refused case, and every other difference, fails. The gate collects
# every failing case (case, reason, status) and reports them all before exiting non-zero.
EXPECTED_REFUSAL={
 'stat-prepared': 'fn-name-reserved', 'stat-compatibility': 'fn-name-reserved', 'stat-wrong-arity': 'fn-name-reserved',
 'stat-wrong-type': 'fn-name-reserved', 'stat-first-error': 'fn-name-reserved', 'named-stat-prepared': 'fn-name-reserved',
 'named-stat-compatibility': 'fn-name-reserved', 'move-order': 'builtin-lazy-args:move', 'move-argument-mutates-source': 'builtin-lazy-args:move',
 'move-first-source-error': 'builtin-lazy-args:move', 'move-first-destination-error': 'builtin-lazy-args:move', 'move-wrong-arity': 'builtin-arity:move',
 'move-cwd-boundary': 'builtin-lazy-args:move', 'copy-order': 'builtin-arity:copy', 'copy-argument-mutates-source': 'builtin-arity:copy',
 'copy-first-source-error': 'builtin-arity:copy', 'copy-first-destination-error': 'builtin-arity:copy', 'copy-cwd-boundary': 'builtin-arity:copy',
 'move-unmatched-source': 'builtin-lazy-args:move', 'move-array-source': 'builtin-lazy-args:move', 'move-bad-source-array': 'builtin-lazy-args:move',
 'move-second-destination-error': 'builtin-lazy-args:move', 'move-second-source-error': 'builtin-lazy-args:move', 'move-spread': 'builtin-arity:move',
 'copy-unmatched-source': 'builtin-arity:copy', 'copy-array-source': 'builtin-arity:copy', 'copy-bad-source-array': 'builtin-arity:copy',
 'copy-second-destination-error': 'builtin-arity:copy', 'copy-second-source-error': 'builtin-arity:copy', 'copy-spread': 'spread',
 'move-loop-location': 'builtin-lazy-args:move', 'move-index-probe-order': 'builtin-lazy-args:move', 'move-root-path-replacement': 'builtin-lazy-args:move',
 'copy-loop-location': 'builtin-arity:copy', 'copy-index-probe-order': 'builtin-arity:copy', 'copy-root-path-replacement': 'builtin-arity:copy',
}
# Owner item 5, combined-b3-b5.md section 85, names these exact seven
# historical stat witnesses. The wider reserved-name feature remains
# post-certification. Keep the general inventory acceptance gate intact.
STAT_REFUSALS={
 'stat-prepared': (9,'b32d829af13ea6e77a6f681d5ffb4aa408c827e93a3b29bd0fd5a3217e5406cc'),
 'stat-compatibility': (9,'ee2998e55223988419b8440f918439afc3e66a8624134949f3d285fe1612907d'),
 'stat-wrong-arity': (9,'0ebf14eb60e630c3149529a91998cb3be99a60c52cd02c64268511581fd752d7'),
 'stat-wrong-type': (9,'3ecf6131306b1d404360b99130aec5d700cdc9fca0fcba96981cbc4a232e445b'),
 'stat-first-error': (7,'ba643606a862c8ca02b15195582befc03e8fe0264918a88f17837d817a119262'),
 'named-stat-prepared': (1,'ee2ef289addd692b6ac148c9e25bc61e2c2476038c185ac1bb3e66aeb12ae1b2'),
 'named-stat-compatibility': (1,'abe12bc9b497157dacb5b7663adb3a24276f273cc2ae39597420d7c7dc57cc74'),
}
# Pure-string contracts by index.
EXPECTED_PURE_REFUSAL={
 48: 'context:assignment-error',
 54: 'unresolved-name', 56: 'opaque:legacy-runtime-error',
}
failures=[]
accepted_refusals=0
executed=0
d181_seen=set()
def fail(case,text):
 if text.startswith(case+': '):text=text[len(case)+2:]
 failures.append('%s: %s'%(case,text));print('FAIL %s: %s'%(case,text),flush=True)
pending=approved_refusal.pending_differences('test-v410-filesystem-recipe-parity')
def judge_refusal(case,reason,pins,argv,cwd,legacy):
 global accepted_refusals
 problem=approved_refusal.pinned_problem(case,pins,reason)
 if problem is None:problem=approved_refusal.check(case,argv,cwd,reason,legacy)
 if problem:fail(case,problem+' [status %s]'%approved_refusal.status(reason));return False
 accepted_refusals+=1
 print('PASS',case,'(VM default: pinned refusal %s, status %s; legacy channel exact)'%(reason,approved_refusal.status(reason)),flush=True)
 return True
p=Path(__file__).resolve().parent/'data';binary=Path(os.environ.get('NIFT','./nift')).resolve();rows=json.loads((p/'v410-filesystem-contracts.json').read_text())
d181_pins=json.loads((p/'v410-filesystem-d181-refusals.json').read_text())
assert len(d181_pins)==30, d181_pins

def snapshot(root):
 return {item.relative_to(root).as_posix(): (
  ('link',os.readlink(item)) if item.is_symlink() else
  ('directory',None) if item.is_dir() else ('file',item.read_bytes()))
  for item in root.rglob('*')}

def d181_problem(pin,result,stats,before,after,root,source):
 if before!=after:
  return 'filesystem changed'
 if result.stdout or stats.get('vm_executions')!=0 or stats.get('legacy_executions')!=0:
  return 'unexpected execution'
 diagnostic=normalize(result.stderr,root).splitlines()
 position=re.fullmatch(r'error: <ORACLE_ROOT>/case\.f:(\d+):(\d+): (.*)',diagnostic[0]) if diagnostic else None
 if (result.returncode!=1 or not position or stats.get('outcome')!='require-error' or stats.get('reason')!=pin['reason']
     or 'note: the program was not executed: no part of it ran' not in diagnostic):
  return 'incorrect refusal diagnostic'
 line,column=map(int,position.groups()[:2])
 if (line,column)!=(stats.get('line'),stats.get('column')) or not 1<=line<=len(source.splitlines()):
  return 'incorrect refusal diagnostic'
 # Cwd-boundary witnesses expand the pinned root token inside the source.
 # Convert its byte length back to the oracle's coordinate space.
 root_bytes=native_path(root).encode()
 prefix=source.splitlines()[line-1].replace('<ORACLE_ROOT>',native_path(root)).encode()[:column-1]
 column=len(prefix.replace(root_bytes,b'<ORACLE_ROOT>'))+1
 if f'error: <ORACLE_ROOT>/case.f:{line}:{column}: {position[3]}'!=pin['diagnostic']:
  return 'incorrect refusal diagnostic'
 return None

def check_d181(case,row,root,path):
 global accepted_refusals
 pin=d181_pins[case]
 assert hashlib.sha256(json.dumps(row,sort_keys=True).encode()).hexdigest()==pin['oracle_sha256'], case
 argv=[str(binary),native_path(path)]
 before=snapshot(root)
 with tempfile.TemporaryDirectory(prefix='d181-stats-') as directory:
  stats_path=Path(directory)/'stats.jsonl'
  env=dict(os.environ,NIFT_VM_STATS=native_path(stats_path))
  env.pop('NIFT_VM',None)
  result=subprocess.run(argv,cwd=root,env=env,capture_output=True,text=True,encoding='utf-8',timeout=60)
  stats_rows=stats_path.read_text().splitlines() if stats_path.exists() else []
  assert len(stats_rows)==1,(case,stats_rows)
  stats=json.loads(stats_rows[0])
 after=snapshot(root)
 problem=d181_problem(pin,result,stats,before,after,root,row['source'])
 assert problem is None, (case,problem,result,stats)
 if case=='pure#48':
  assert d181_problem(pin,result,dict(stats,vm_executions=1),before,after,root,row['source'])=='unexpected execution'
  assert d181_problem(pin,result,dict(stats,legacy_executions=1),before,after,root,row['source'])=='unexpected execution'
  assert d181_problem(pin,result,stats,before,dict(after,unexpected=('file',b'x')),root,row['source'])=='filesystem changed'
  wrong_position=dict(pin,diagnostic=pin['diagnostic'].replace(':1:58:',':1:59:'))
  assert d181_problem(wrong_position,result,stats,before,after,root,row['source'])=='incorrect refusal diagnostic'
  assert d181_problem(dict(pin,reason='wrong-reason'),result,stats,before,after,root,row['source'])=='incorrect refusal diagnostic'
 legacy=subprocess.run(argv,cwd=root,env=dict(os.environ,NIFT_VM='0'),capture_output=True,
                       text=True,encoding='utf-8',timeout=60)
 assert (legacy.returncode,normalize(legacy.stdout,root),normalize(legacy.stderr,root))==(row['exit'],row['stdout'],row['stderr']), (case,legacy)
 if 'files' in row:
  files={item.relative_to(root).as_posix():item.read_text() for item in root.rglob('*') if item.is_file() and item!=path}
  assert files==row['files'], (case,files)
 else:
  assert snapshot(root)==before,case
 d181_seen.add(case)
 accepted_refusals+=1
 print('PASS',case,'(D-181 exact refusal and independent legacy oracle)',flush=True)

for row in rows:
 with tempfile.TemporaryDirectory(prefix='nift-filesystem-oracle-') as directory:
  root=Path(directory).resolve();(root/'one').mkdir();(root/'two').mkdir()
  initial_files={'a':b'A','b':b'B','one/a':b'ONE','two/a':b'TWO'}
  for file,data in initial_files.items(): (root/file).write_bytes(data)
  source=row['source'].replace('<ORACLE_ROOT>',native_path(root));path=root/'case.f';path.write_text(source+'\n')
  if row['name'] in d181_pins and os.environ.get('NIFT_VM')!='0':
   check_d181(row['name'],row,root,path)
   continue
  q=subprocess.run([str(binary),native_path(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  reason=approved_refusal.refusal_reason(q.stderr)
  if row['name'] in STAT_REFUSALS and os.environ.get('NIFT_VM')!='0':
   column,digest=STAT_REFUSALS[row['name']]
   actual_files={f.relative_to(root).as_posix():f.read_bytes() for f in root.rglob('*') if f.is_file() and f!=path}
   assert actual_files==initial_files, actual_files
   assert hashlib.sha256(row['source'].encode()).hexdigest()==digest, row['name']
   diagnostic=(f'error: <ORACLE_ROOT>/case.f:1:{column}: not compiled for the VM (fn-name-reserved): '
               'a function named like a built-in is not supported by the Runtime 2.0 VM yet')
   assert normalize(q.stderr,root).splitlines()[0]==diagnostic, q
   def stat_legacy(lq):
    files={f.relative_to(root).as_posix():f.read_text() for f in root.rglob('*') if f.is_file() and f!=path}
    got=dict(exit=lq.returncode,stdout=normalize(lq.stdout,root),stderr=normalize(lq.stderr,root),files=files)
    expected={k:row[k] for k in got}
    return None if got==expected else repr((got,expected))
   problem=approved_refusal.check(row['name'],[str(binary),native_path(path)],root,'fn-name-reserved',stat_legacy,
                                  rows={'fn-name-reserved':{'status':'approved'}})
   assert problem is None, problem
   accepted_refusals+=1
   print('PASS',row['name'],'(exact reserved-name refusal; legacy channel exact)',flush=True)
   continue
  if reason is not None:
   actual_files={f.relative_to(root).as_posix():f.read_bytes() for f in root.rglob('*') if f.is_file() and f!=path}
   if actual_files!=initial_files:
    fail(row['name'],'files changed by a refused program');continue
   def legacy(lq,row=row,root=root):
    files={f.relative_to(root).as_posix():f.read_text() for f in root.rglob('*') if f.is_file() and f.name!='case.f'}
    actual=dict(exit=lq.returncode,stdout=normalize(lq.stdout,root),stderr=normalize(lq.stderr,root),files=files)
    return None if actual=={k:row[k] for k in actual} else repr((actual,{k:row[k] for k in actual}))
   judge_refusal(row['name'],reason,EXPECTED_REFUSAL,[str(binary),native_path(path)],root,legacy)
   continue
  files={f.relative_to(root).as_posix():f.read_text() for f in root.rglob('*') if f.is_file() and f.name!='case.f'}
  actual=dict(exit=q.returncode,stdout=normalize(q.stdout,root),stderr=normalize(q.stderr,root),files=files);expected={k:row[k] for k in actual}
  if row['name']=='copy-wrong-arity' and os.environ.get('NIFT_VM')!='0':
   # D-179: this single-argument value copy must evaluate its operand once.
   # Independently retain the historical second evaluation and file state.
   assert row['source']=='n:=0; fn(src()) { n+=1; print(n); return "a" }; j:=0; while(j<1) { copy(src()); j+=1 }'
   legacy=subprocess.run([str(binary),native_path(path)],cwd=root,env=dict(os.environ,NIFT_VM='0'),
                         capture_output=True,text=True,encoding='utf-8',timeout=60)
   legacy_files={f.relative_to(root).as_posix():f.read_text() for f in root.rglob('*') if f.is_file() and f!=path}
   assert (legacy.returncode,legacy.stdout,normalize(legacy.stderr,root),legacy_files)==(
    expected['exit'],'1\n2\n',expected['stderr'],expected['files']), legacy
   assert expected['stdout']=='1\n2\n', expected
   expected['stdout']='1\n'
  if actual!=expected:
   note=' (unapproved VM/legacy difference, PENDING-OWNER: tests/runtime2/cp25/pending_differences.tsv)' if row['name'] in pending else ''
   fail(row['name'],'VM %r != recorded %r%s'%(actual,expected,note));continue
  executed+=1
  print('PASS',row['name'],flush=True)

rows=json.loads((p/'v410-filesystem-pure-contracts.json').read_text())
with tempfile.TemporaryDirectory(prefix='nift-pure-oracle-') as directory:
 root=Path(directory).resolve()
 for i,row in enumerate(rows):
  path=root/'case.f';path.write_text(row['source']+'\n')
  case='pure#%d'%i
  if case in d181_pins and os.environ.get('NIFT_VM')!='0':
   check_d181(case,row,root,path)
   continue
  q=subprocess.run([str(binary),native_path(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  reason=approved_refusal.refusal_reason(q.stderr)
  if reason is not None:
   judge_refusal('pure#%d'%i,reason,{'pure#%d'%k:v for k,v in EXPECTED_PURE_REFUSAL.items()},[str(binary),native_path(path)],root,
                 lambda lq,row=row,root=root:None if (lq.returncode,lq.stdout,normalize(lq.stderr,root))==(row['exit'],row['stdout'],row['stderr']) else repr((lq.returncode,lq.stdout,lq.stderr)))
   continue
  actual=(q.returncode,q.stdout,normalize(q.stderr,root))
  if actual!=(row['exit'],row['stdout'],row['stderr']):fail('pure#%d'%i,'VM %r != recorded %r'%(actual,(row['exit'],row['stdout'],row['stderr'])))
  else:executed+=1
if os.environ.get('NIFT_VM')!='0':
 assert d181_seen==d181_pins.keys(),d181_pins.keys()-d181_seen
 with tempfile.TemporaryDirectory(prefix='nift-grouped-postfix-') as directory:
  root=Path(directory).resolve();path=root/'case.f'
  for source in ('p:="a/b";print((p) .split("/").last())',
                 'print(("a"+"b") .stringify())',
                 'p:="a/b";print((p).split ("/").last())'):
   path.write_text('print("REFUSAL_EXECUTION_SENTINEL")\n'+source+'\n')
   q=subprocess.run([str(binary),native_path(path)],cwd=root,env=dict(os.environ,NIFT_VM='require'),
                    capture_output=True,text=True,encoding='utf-8',timeout=60)
   assert (q.returncode,q.stdout)==(1,''), (source,q)
   assert approved_refusal.refusal_reason(q.stderr)=='opaque:legacy-runtime-error', (source,q)
   assert 'note: the program was not executed: no part of it ran' in q.stderr and set(root.iterdir())=={path}, (source,q)
 print('PASS grouped postfix irregular-spacing controls')
 with tempfile.TemporaryDirectory(prefix='nift-spaced-split-') as directory:
  root=Path(directory).resolve();path=root/'case.f'
  path.write_text('p:="a/b"; print(p.split\t("/").last())\n')
  for mode in ('0','require'):
   q=subprocess.run([str(binary),native_path(path)],cwd=root,env=dict(os.environ,NIFT_VM=mode),
                    capture_output=True,text=True,encoding='utf-8',timeout=60)
   assert (q.returncode,q.stdout,q.stderr)==(0,'b\n',''), (mode,q)
  path.write_text('p:="a/b"; fn(d()) { print("argument"); return open("missing") }; '
                  'j:=0; while(j<1) { print(p.split (d()).last()); j+=1 }\n')
  q=subprocess.run([str(binary),native_path(path)],cwd=root,env=dict(os.environ,NIFT_VM='require'),
                   capture_output=True,text=True,encoding='utf-8',timeout=60)
  assert (q.returncode,q.stdout)==(1,''), q
  assert approved_refusal.refusal_reason(q.stderr)=='postfix-spelling', q
  (root/'tagged').write_bytes(b'\x1fnift:file:7')
  path.write_text('print("REFUSAL_EXECUTION_SENTINEL"); p:=open("tagged"); print(p.split ("/").last())\n')
  q=subprocess.run([str(binary),native_path(path)],cwd=root,env=dict(os.environ,NIFT_VM='require'),
                   capture_output=True,text=True,encoding='utf-8',timeout=60)
  assert (q.returncode,q.stdout)==(1,''), q
  assert approved_refusal.refusal_reason(q.stderr)=='postfix-spelling', q
 print('PASS spaced split tab, faulting-delimiter and tagged-source controls')
 with tempfile.TemporaryDirectory(prefix='nift-stat-provider-') as directory:
  root=Path(directory).resolve();path=root/'case.f';data=root/'a'
  data.write_bytes(b'A')
  path.write_text('n:=0;fn(fixture_path()){n+=1;print(n);return "a"};'
                  'j:=0;while(j<2){s:=stat(fixture_path());print(s.exists);j+=1};print(n)\n')
  for engine,stdout in (('require','1\ntrue\n2\ntrue\n2\n'),
                        ('0','1\n2\ntrue\n3\n4\ntrue\n4\n')):
   q=subprocess.run([str(binary),native_path(path)],cwd=root,env=dict(os.environ,NIFT_VM=engine),
                    capture_output=True,text=True,encoding='utf-8',timeout=60)
   assert (q.returncode,q.stdout,q.stderr)==(0,stdout,''), (engine,q)
   assert set(root.iterdir())=={path,data} and data.read_bytes()==b'A', engine
 print('PASS stat provider effects with an admissible name')
 with tempfile.TemporaryDirectory(prefix='nift-readonly-arity-') as directory:
  root=Path(directory).resolve();path=root/'case.f'
  for receiver,call,message in (
   ('p','split(arg(),arg())','split: expected one delimiter'),
   ('p.split("/")','last(arg())','last: expected no arguments'),
  ):
   prefix='p:="a/b";fn(arg()){print("ARG");return "/"};print("entered");print('
   src=prefix+receiver+'.'+call+')'
   path.write_text(src+'\n')
   for mode in ('0','require'):
    q=subprocess.run([str(binary),native_path(path)],cwd=root,env=dict(os.environ,NIFT_VM=mode),
                     capture_output=True,text=True,encoding='utf-8',timeout=60)
    assert (q.returncode,q.stdout,normalize(q.stderr,root))==(
     1,'entered\n',f'error: <ORACLE_ROOT>/case.f:1:{len(prefix)+1}: {message}\n'), (mode,q)
   assert set(root.iterdir())=={path}
  for source,reason in (('s:=open("data");s.split()','builtin-arity:split'),
                        ('s:="$[open("data")]";s.split()','opaque:legacy-runtime-error'),
                        ('fn(g()){return [1].last(2)};g()','builtin-arity:last'),
                        ('st:=ifstream("data")\ns:=""\nst >> s\nclose(st)\ns.split()', 'builtin-arity:split')):
   path.write_text(source+'\n')
   q=subprocess.run([str(binary),native_path(path)],cwd=root,env=dict(os.environ,NIFT_VM='require'),
                    capture_output=True,text=True,encoding='utf-8',timeout=60)
   assert (q.returncode,q.stdout,approved_refusal.refusal_reason(q.stderr))==(1,'',reason), q
 print('PASS readonly arity reports at execution and does not evaluate invalid operands')
if failures:
 print('FAIL filesystem recipe parity: %d failing case(s):'%len(failures))
 for f in failures:print('  '+f)
 sys.exit(1)
assert executed+accepted_refusals==97,(executed,accepted_refusals)
print(f'PASS all 97 filesystem and pure-string contracts: {executed} executed, {accepted_refusals} approved refusals')
