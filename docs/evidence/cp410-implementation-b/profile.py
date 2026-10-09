from pathlib import Path
import subprocess,re,json
p=Path('.build/cp410-implementation/traversal').resolve();rows=[]
for label,binary in [('accepted',Path('nift').resolve()),('shared',p/'nift')]:
 cg=p/(label+'.callgrind');fixture=Path('.build/cp410-shell/official-traverse-100000').resolve()
 q=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(binary),str(fixture/'source.f')],cwd=fixture,capture_output=True,text=True,check=True);assert q.stdout=='120000\n';rows.append(dict(label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1])));(p/'instructions.json').write_text(json.dumps(rows,indent=2)+'\n');print(rows[-1],flush=True)
