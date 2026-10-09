from pathlib import Path
import subprocess,json,re
p=Path('.build/cp410-capture-frame/jsonic').resolve();rows=[]
for label in ('baseline','32'):
 cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(p),str(p/('baseline-probe.cpp' if label=='baseline' else 'probe.cpp')),'-o',str(p/('plain-'+label))]
 if label!='baseline':cmd.insert(4,'-DCP_THRESHOLD=32')
 subprocess.run(cmd,check=True)
for n in (8,16,32,64,128,512,8000):
 for kind in ('unique','early','middle','end','escaped'):
  expected=None
  for label in ('baseline','32'):
   cg=p/f'plain-{n}-{kind}-{label}.callgrind';r=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+str(cg),str(p/('plain-'+label)),str(p/f'{kind}-{n}.json'),'reject'],capture_output=True,text=True,check=True)
   if expected is None:expected=r.stdout
   assert r.stdout==expected
   rows.append({'width':n,'kind':kind,'threshold':label,'instructions':int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),'stdout':expected})
  print(n,kind,'PASS',flush=True)
  Path('docs/evidence/cp410-capture-frame/jsonic-plain.json').write_text(json.dumps(rows,indent=2)+'\n')
print('COMPLETE 70 ordinary allocator Jsonic instruction comparisons')
