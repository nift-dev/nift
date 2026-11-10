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
# The VM-default refusal expected per case (review of C83-C99, P2-1); any other refused case fails.
EXPECTED_REFUSAL={n:'fn-name-reserved' for n in ('stat-prepared','stat-compatibility','stat-wrong-arity','stat-wrong-type',
                                                 'stat-first-error','named-stat-prepared','named-stat-compatibility')}
p=Path(__file__).resolve().parent/'data';binary=Path(os.environ.get('NIFT','./nift')).resolve();rows=json.loads((p/'v410-filesystem-contracts.json').read_text())
for row in rows:
 with tempfile.TemporaryDirectory(prefix='nift-filesystem-oracle-') as directory:
  root=Path(directory).resolve();(root/'one').mkdir();(root/'two').mkdir()
  for file,data in [('a','A'),('b','B'),('one/a','ONE'),('two/a','TWO')]: (root/file).write_text(data)
  source=row['source'].replace('<ORACLE_ROOT>',native_path(root));path=root/'case.f';path.write_text(source+'\n')
  q=subprocess.run([str(binary),native_path(path)],cwd=root,capture_output=True,text=True,encoding='utf-8',timeout=60)
  reason=approved_refusal.refusal_reason(q.stderr)
  if reason is not None:
   # Owner 2026-11-10 13:26, item 5, and review of C83-C99, P2-1 (combined-b3-b5.md sections 85 and 89):
   # a VM-default refusal counts only for a case whose reason is pinned below and whose inventory status
   # is approved or deferred-named; the refused run must leave the fixture untouched, and the recorded
   # contract is checked on the legacy channel (NIFT_VM=0) by approved_refusal.check.
   problem=approved_refusal.pinned_problem(row['name'],EXPECTED_REFUSAL,reason);assert problem is None,problem
   assert not [f for f in root.rglob('*') if f.is_file() and f.name!='case.f' and f.relative_to(root).as_posix() not in ('a','b','one/a','two/a')],(row['name'],'files changed by a refused program')
   def legacy(lq,row=row,root=root):
    files={f.relative_to(root).as_posix():f.read_text() for f in root.rglob('*') if f.is_file() and f.name!='case.f'}
    actual=dict(exit=lq.returncode,stdout=normalize(lq.stdout,root),stderr=normalize(lq.stderr,root),files=files)
    return None if actual=={k:row[k] for k in actual} else repr((actual,{k:row[k] for k in actual}))
   problem=approved_refusal.check(row['name'],[str(binary),native_path(path)],root,reason,legacy);assert problem is None,problem
   print('PASS',row['name'],'(VM default: pinned refusal %s, status %s; legacy channel exact)'%(reason,approved_refusal.status(reason)),flush=True)
   continue
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
