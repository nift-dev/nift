from pathlib import Path
import subprocess,json,re
p=Path('.build/cp410-implementation').resolve();rows=[]
binaries={'baseline':p/'baseline-nift','snapshot':Path('nift').resolve()}
for n in (2000,4000,8000,16000):
 for width in (0,5,16,50,100):
  for kind in ('identity','captured','map','filter','group','closure'):
   prefix=''.join(f'u{k} := {k}\n' for k in range(width));setup=f'a := []; i := 1; while(i<={n}) {{ a.push({n}-i+1); i+=1 }}\n'
   expression={'identity':'a.sort_by(x => x)','captured':'a.sort_by(x => x+v)','map':'a.map(x => x+v)','filter':'a.filter(x => x>v)','group':'a.group_by(x => x%v)','closure':'a.map(f)'}[kind]
   source=prefix+'v := 2\n'+setup+('fn(mk(v)) { return x => x+v }; f := mk(2)\n' if kind=='closure' else '')+'r := '+expression+'\nprint(r.stringify())\n'
   file=p/f'matrix-{n}-{width}-{kind}.f';file.write_text(source);expected=None
   for label,binary in binaries.items():
    cg=p/f'matrix-{n}-{width}-{kind}-{label}.callgrind';q=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(binary),str(file)],capture_output=True,text=True,check=True)
    if expected is None:expected=q.stdout
    assert q.stdout==expected
    rows.append(dict(n=n,width=width,kind=kind,label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1])))
   (p/'matrix-instructions.json').write_text(json.dumps(rows,indent=2)+'\n');print(n,width,kind,'PASS',flush=True)
print('COMPLETE 240 exact-output whole-process instruction profiles')
