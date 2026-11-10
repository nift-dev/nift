from pathlib import Path
import subprocess,tempfile,json,os,sys
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
# Pure-string contracts by index.
EXPECTED_PURE_REFUSAL={
 0: 'postfix-spelling', 1: 'postfix-spelling', 2: 'postfix-spelling',
 3: 'postfix-spelling', 4: 'postfix-spelling', 5: 'postfix-spelling',
 6: 'postfix-spelling', 7: 'postfix-spelling', 8: 'postfix-spelling',
 9: 'postfix-spelling', 10: 'postfix-spelling', 11: 'postfix-spelling',
 12: 'postfix-spelling', 13: 'postfix-spelling', 14: 'postfix-spelling',
 15: 'postfix-spelling', 16: 'postfix-spelling', 17: 'postfix-spelling',
 18: 'postfix-spelling', 19: 'postfix-spelling', 20: 'postfix-spelling',
 21: 'postfix-spelling', 22: 'postfix-spelling', 23: 'postfix-spelling',
 24: 'postfix-spelling', 25: 'postfix-spelling', 26: 'postfix-spelling',
 27: 'postfix-spelling', 28: 'postfix-spelling', 29: 'postfix-spelling',
 30: 'postfix-spelling', 31: 'postfix-spelling', 32: 'postfix-spelling',
 33: 'postfix-spelling', 34: 'postfix-spelling', 35: 'postfix-spelling',
 36: 'postfix-spelling', 37: 'postfix-spelling', 38: 'postfix-spelling',
 39: 'postfix-spelling', 40: 'postfix-spelling', 41: 'postfix-spelling',
 42: 'postfix-spelling', 43: 'postfix-spelling', 44: 'postfix-spelling',
 45: 'postfix-spelling', 46: 'postfix-spelling', 47: 'postfix-spelling',
 48: 'context:assignment-error', 52: 'builtin-arity:split', 53: 'builtin-arity:last',
 54: 'unresolved-name', 56: 'opaque:legacy-runtime-error', 57: 'postfix-spelling',
}
failures=[]
def fail(case,text):
 if text.startswith(case+': '):text=text[len(case)+2:]
 failures.append('%s: %s'%(case,text));print('FAIL %s: %s'%(case,text),flush=True)
pending=approved_refusal.pending_differences('test-v410-filesystem-recipe-parity')
def judge_refusal(case,reason,pins,argv,cwd,legacy):
 problem=approved_refusal.pinned_problem(case,pins,reason)
 if problem is None:problem=approved_refusal.check(case,argv,cwd,reason,legacy)
 if problem:fail(case,problem+' [status %s]'%approved_refusal.status(reason));return False
 print('PASS',case,'(VM default: pinned refusal %s, status %s; legacy channel exact)'%(reason,approved_refusal.status(reason)),flush=True)
 return True
p=Path(__file__).resolve().parent/'data';binary=Path(os.environ.get('NIFT','./nift')).resolve();rows=json.loads((p/'v410-filesystem-contracts.json').read_text())
for row in rows:
 with tempfile.TemporaryDirectory(prefix='nift-filesystem-oracle-') as directory:
  root=Path(directory).resolve();(root/'one').mkdir();(root/'two').mkdir()
  for file,data in [('a','A'),('b','B'),('one/a','ONE'),('two/a','TWO')]: (root/file).write_text(data)
  source=row['source'].replace('<ORACLE_ROOT>',native_path(root));path=root/'case.f';path.write_text(source+'\n')
  q=subprocess.run([str(binary),native_path(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  reason=approved_refusal.refusal_reason(q.stderr)
  if reason is not None:
   if [f for f in root.rglob('*') if f.is_file() and f.name!='case.f' and f.relative_to(root).as_posix() not in ('a','b','one/a','two/a')]:
    fail(row['name'],'files changed by a refused program');continue
   def legacy(lq,row=row,root=root):
    files={f.relative_to(root).as_posix():f.read_text() for f in root.rglob('*') if f.is_file() and f.name!='case.f'}
    actual=dict(exit=lq.returncode,stdout=normalize(lq.stdout,root),stderr=normalize(lq.stderr,root),files=files)
    return None if actual=={k:row[k] for k in actual} else repr((actual,{k:row[k] for k in actual}))
   judge_refusal(row['name'],reason,EXPECTED_REFUSAL,[str(binary),native_path(path)],root,legacy)
   continue
  files={f.relative_to(root).as_posix():f.read_text() for f in root.rglob('*') if f.is_file() and f.name!='case.f'}
  actual=dict(exit=q.returncode,stdout=normalize(q.stdout,root),stderr=normalize(q.stderr,root),files=files);expected={k:row[k] for k in actual}
  if actual!=expected:
   note=' (unapproved VM/legacy difference, PENDING-OWNER: tests/runtime2/cp25/pending_differences.tsv)' if row['name'] in pending else ''
   fail(row['name'],'VM %r != recorded %r%s'%(actual,expected,note));continue
  print('PASS',row['name'],flush=True)

rows=json.loads((p/'v410-filesystem-pure-contracts.json').read_text())
with tempfile.TemporaryDirectory(prefix='nift-pure-oracle-') as directory:
 root=Path(directory).resolve()
 for i,row in enumerate(rows):
  path=root/'case.f';path.write_text(row['source']+'\n')
  q=subprocess.run([str(binary),native_path(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  reason=approved_refusal.refusal_reason(q.stderr)
  if reason is not None:
   judge_refusal('pure#%d'%i,reason,{'pure#%d'%k:v for k,v in EXPECTED_PURE_REFUSAL.items()},[str(binary),native_path(path)],root,
                 lambda lq,row=row,root=root:None if (lq.returncode,lq.stdout,normalize(lq.stderr,root))==(row['exit'],row['stdout'],row['stderr']) else repr((lq.returncode,lq.stdout,lq.stderr)))
   continue
  actual=(q.returncode,q.stdout,normalize(q.stderr,root))
  if actual!=(row['exit'],row['stdout'],row['stderr']):fail('pure#%d'%i,'VM %r != recorded %r'%(actual,(row['exit'],row['stdout'],row['stderr'])))
if failures:
 print('FAIL filesystem recipe parity: %d failing case(s):'%len(failures))
 for f in failures:print('  '+f)
 sys.exit(1)
print('PASS all filesystem and pure-string contracts')
