from pathlib import Path
import subprocess,tempfile,json
p=Path(__file__).resolve().parent;binaries=[Path('.build/cp410-implementation/strings/prototype/nift').resolve(),p/'nift'];cases=[]
for mode in ['r','rw','w','a']:
 cases.append(('open-'+mode,'f:=file("data");f.open("'+mode+'");print(f.modified());f.revert();print(f.modified());f.close();print(f.exists())'))
for operation in ['write("xy")','write("")','write_line("xy")','write_bytes(bytes([0,255,65]))','write(bytes([0,255,65]))','write_val(42)','append("xy")','append("")','prepend("xy")','insert(2,"xy")','insert_before("CD","xy")','insert_after("CD","xy")','replace("CD","xy")','replace("CD","CD")','replace("missing","xy")','replace_once("CD","xy")']:
 cases.append(('mutate-'+operation,'f:=file("data");f.open("rw");f.'+operation+';print(f.modified());f.revert();print(f.modified());print(f.read_all().stringify());f.close()'))
cases += [
 ('save-then-revert','f:=file("data");g:=f;f.open("rw");f.write("xy");g.save();print(f.modified());f.append("tail");print(g.modified());g.revert();print(f.read_all().stringify());f.close()'),
 ('overwrite-initial-revert','f:=file("data");f.open("w");print(f.modified());f.write("xy");f.revert();print(f.modified());f.close();print(open("data"))'),
 ('new-empty-write','f:=file("new");f.open("w");print(f.modified());f.write("");print(f.modified());f.close();print(f.exists())'),
 ('append-new-empty-write','f:=file("new");f.open("a");print(f.modified());f.append("");print(f.modified());f.close();print(f.exists())'),
 ('reopen-external-edit','f:=file("data");f.open("rw");f.close();g:=file("data");g.open("w");g.write("external");g.save();g.close();f.open("rw");print(f.read_all());f.close()'),
 ('opaque-closed-reopen','f:=file("data");f.open("rw");f.close();tag:="\u001fnift:file:1";tag.open("rw");tag.write("xy");tag.save();tag.close();print(open("data"));print(file("new").stringify())'),
 ('write-argument-saves','f:=file("data");f.open("rw");n:=0;fn(d()){n+=1;f.write("saved");f.save();return "xy"};i:=0;while(i<1){f.write(d());i+=1};print(n);print(f.modified());f.revert();print(f.read_all());f.close()'),
 ('write-argument-reverts','f:=file("data");f.open("rw");n:=0;fn(d()){n+=1;f.revert();return "xy"};i:=0;while(i<1){f.write(d());i+=1};print(n);f.revert();print(f.read_all());f.close()'),
 ('write-argument-closes','f:=file("data");f.open("rw");fn(d()){f.close();return "xy"};f.write(d());print(f.modified())'),
 ('closed-write-factory-count','f:=file("data");n:=0;fn(d()){n+=1;print(n);return "xy"};i:=0;while(i<1){f.write(d());i+=1}'),
 ('readonly-write-factory-count','f:=file("data");f.open("r");n:=0;fn(d()){n+=1;print(n);return "xy"};i:=0;while(i<1){f.write(d());i+=1}'),
 ('failed-save-revert','f:=file("missing/child");f.open("w");f.write("xy");try{f.save()}catch(e){print(e.message)};print(f.modified());f.revert();print(f.modified());f.close()'),
 ('duplicate-replace-once-error','f:=file("data");f.open("rw");f.replace_once("A","xy")'),
]
cases += [('receiver-factory-count', 'f:=file("data");f.open("rw");n:=0;fn(r()){n+=1;return f};r().write("xy");print(n);f.revert();f.close()'), ('factory-path-counter', 'n:=0;fn(d()){n+=1;return "data"};f:=file(d());print(n);print(f.exists());print(f.stringify())'), ('factory-changing-cwd', 'n:=0;fn(d()){n+=1;cd("sub");return "data"};f:=file(d());print(n);print(f.path());print(f.exists())'), ('open-mode-factory-cwd', 'f:=file("data");n:=0;fn(d()){n+=1;cd("sub");return "rw"};f.open(d());print(n);print(f.read_all());f.close()'), ('closed-file-copy', 'f:=file("data");g:=f.copy("copied");print(f.path());print(g.stringify());print(open("copied"))'), ('closed-file-move', 'f:=file("data");g:=f.move("renamed");print(f.path());print(g.stringify());print(open("renamed"))'), ('closed-file-remove', 'f:=file("data");f.remove();print(f.exists())'), ('open-copy-precondition-counter', 'f:=file("data");f.open("r");n:=0;fn(d()){n+=1;print(n);return "copied"};f.copy(d())'), ('copy-invalid-argument', 'f:=file("data");f.copy(["copied"])'), ('unknown-file-tag', 'tag:="\\u001fnift:file:999";print(tag.path())'), ('nonfile-receiver-counter', 'x:="text";n:=0;fn(d()){n+=1;print(n);return "value"};x.write(d())'), ('read-val-token-contract', 'f:=file("data");f.open("w");f.write("42 \\"hi\\" [] {}");f.save();f.close();f.open("r");print(f.read_val());print(f.read_val());print(f.read_val().stringify());print(f.read_val().stringify());f.close()'), ('invalid-mode-arity', 'f:=file("data");f.open("w","r")'), ('save-argument-arity', 'f:=file("data");f.open("rw");f.save(1)'), ('empty-write-return-to-clean', 'f:=file("data");f.open("w");f.write("ABCDEFGH");print(f.modified());f.close();print(open("data"))'), ('nested-method-depth-20', 'f:=file("data");fn(id(x)){return x};print(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(f.exists())))))))))))))))))))))'), ('nested-method-depth-40', 'f:=file("data");fn(id(x)){return x};print(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(f.exists())))))))))))))))))))))))))))))))))))))))))'), ('nested-method-depth-48', 'f:=file("data");fn(id(x)){return x};print(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(f.exists())))))))))))))))))))))))))))))))))))))))))))))))))'), ('nested-method-depth-50', 'f:=file("data");fn(id(x)){return x};print(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(f.exists())))))))))))))))))))))))))))))))))))))))))))))))))))'), ('nested-method-depth-60', 'f:=file("data");fn(id(x)){return x};print(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(id(f.exists())))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))')]
for kind in ['named','html_escape','type','min','method','method-argument']:
 for n in [20,40,48,50,60,80,96,110]:
  expr='f.exists()' if kind=='named' else '1' if kind=='min' else '"x"'
  if kind=='method':expr+=' .trim()'*n
  elif kind=='method-argument':
   for _ in range(n):expr='"x".replace("x",'+expr+')'
  else:expr=('id(' if kind=='named' else kind+'(')*n+expr+')'*n
  cases.append(('stack-'+kind+'-'+str(n),'f:=file("data");fn(id(x)){return x};print('+expr+')'))
cases.append(('write-preserves-string-slots','payload:=open("data");alias:=payload;f:=file("out");f.open("w");f.write(payload);print(payload);print(alias);f.save();f.close();print(open("out"))'))
rows=[]
with tempfile.TemporaryDirectory(prefix='nift-buffer-contract-') as directory:
 root=Path(directory)
 for name,source in cases:
  values=[]
  for binary in binaries:
   for item in list(root.iterdir()):
    if item.is_file():item.unlink()
   (root/'sub/sub').mkdir(parents=True,exist_ok=True)
   for nested in ['sub/data','sub/sub/data']:(root/nested).write_bytes(b'ABCDEFGH')
   (root/'data').write_bytes(b'ABCDEFGH');file=root/'case.f';file.write_text(source+'\n')
   q=subprocess.run([str(binary),str(file)],cwd=root,text=True,capture_output=True,encoding='utf-8',timeout=60)
   values.append(dict(exit=q.returncode,stdout=q.stdout.replace(str(root),'<ORACLE_ROOT>'),stderr=q.stderr.replace(str(root),'<ORACLE_ROOT>'),files={f.relative_to(root).as_posix():f.read_bytes().hex() for f in root.rglob('*') if f.is_file() and f.name!='case.f'}))
  assert values[0]==values[1],(name,values)
  rows.append(dict(name=name,source=source,**values[0]));print('PASS',name,flush=True)
(p/'contracts.json').write_text(json.dumps(rows,indent=2)+'\n');print('PASS',len(rows),'exact buffer / reentry / save / shell identity contracts')
