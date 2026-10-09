from pathlib import Path
import subprocess,json,re
p=Path(__file__).parent.resolve();code=json.loads((p/'string-direct-sources.json').read_text())['utf8-large'];rows=[];expected=next(r['stdout_sha256'] for r in json.loads((p/'string-direct-instructions.json').read_text()) if r['name']=='utf8-large')
for label in ['large-ast-only','string-direct-large']:
 cg=p/f'{label}.callgrind';r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(p/(label+'-nift')),'-'],input=code,text=True,capture_output=True,check=True);assert __import__('hashlib').sha256(r.stdout.encode()).hexdigest()==expected
 rows.append(dict(label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),stdout_sha256=expected,scope='exact original pure large literal fixture; scratch 64KiB prepared-source ceiling, original nesting guard preserved; general error/side-effect/resource-limit contract uncertified'));(p/'large-ast-instructions.json').write_text(json.dumps(rows,indent=2)+'\n');print(label,rows[-1]['instructions'],flush=True)
