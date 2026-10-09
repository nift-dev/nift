from pathlib import Path
import subprocess,json,tempfile,itertools
p=Path(__file__).resolve().parent;binaries=[Path('.build/cp410-implementation/filesystem/nift').resolve(),p/'prototype/nift'];cases=[]
for size in [8192,16384,32768,65536,131072]:
 literal=json.dumps(('é_λ_Z'*(size//8+1))[:size//2],ensure_ascii=False)
 for old,new in [('Z','q'),('_','__'),('_',''),('_','_'),('missing','x')]:
  cases.append((f'payload-{size}-{old}-{new}',f's:="";i:=0;while(i<3){{s={literal}.to_upper().replace({json.dumps(old)},{json.dumps(new)});i+=1}};print(s.stringify())'))
large=json.dumps('a_'*5000)
for suffix in ['.to_upper().replace("A","x")','.to_upper().replace("A","x",)','.to_upper().replace(,"A","x")','.to_upper().replace("","x")','.to_upper(,).replace("A","x")','.trim().replace("a","x")','.to_lower().replace("a","x")','.replace("a",7)']:
 cases.append(('shape-'+suffix,'s:="";i:=0;while(i<2){s='+large+suffix+';i+=1};print(s.stringify())'))
cases += [
 ('mixed-body-counts','n:=0;fn(f()){n+=1;return "a"};s:="";i:=0;while(i<2){s='+large+'.to_upper().replace("A","x");print(f().to_upper());i+=1};print(n)'),
 ('receiver-factory','n:=0;fn(f()){n+=1;return '+large+'};i:=0;while(i<2){print(f().to_upper().replace("A","x").size());i+=1};print(n)'),
 ('argument-factory','n:=0;fn(f()){n+=1;return "A"};i:=0;while(i<2){print('+large+'.to_upper().replace(f(),"x").size());i+=1};print(n)'),
 ('nested-error','s:="";i:=0;while(i<1){s='+large+'.to_upper().replace(no_such_name,"x");i+=1}'),
]
rows=[]
with tempfile.TemporaryDirectory() as directory:
 root=Path(directory)
 for name,source in cases:
  file=root/'case.f';file.write_text(source+'\n');values=[]
  for binary in binaries:
   q=subprocess.run([str(binary),str(file)],cwd=root,text=True,capture_output=True,timeout=60);values.append(dict(exit=q.returncode,stdout=q.stdout,stderr=q.stderr.replace(str(root),'<ORACLE_ROOT>')))
  assert values[0]==values[1],(name,values)
  rows.append(dict(name=name,source=source,**values[0]));print('PASS',name,flush=True)
(p/'contracts.json').write_text(json.dumps(rows,indent=2,ensure_ascii=False)+'\n');print('PASS',len(rows),'exact large payload / factory / error contracts')
