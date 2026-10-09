from pathlib import Path
import subprocess,json,re
p=Path(__file__).parent.resolve();wave=Path('.build/cp410-wave3').resolve();rows=[]
for name,code in json.loads((p/'string-direct-sources.json').read_text()).items():
 for label,binary in [('current',wave/'string-nift'),('prepared-prototype',p/'string-direct-nift')]:
  cg=p/f'string-direct-{name}-{label}.callgrind';r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(binary),'-'],input=code,capture_output=True,text=True,check=True)
  if label=='current':expected=r.stdout
  assert r.stdout==expected
  rows.append(dict(name=name,label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),stdout_sha256=__import__('hashlib').sha256(r.stdout.encode()).hexdigest(),semantic_scope='pure literal operands only; known receiver/argument side-effect mismatch prevents general production promotion'))
  (p/'string-direct-instructions.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(name,'PASS pure outputs',flush=True)
