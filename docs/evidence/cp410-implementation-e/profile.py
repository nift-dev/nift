from pathlib import Path
import subprocess,json,re
p=Path(__file__).resolve().parent;source=json.loads(Path('.build/cp410-silverback/string-direct-sources.json').read_text())['utf8-large'];rows=[]
for label,binary in [('accepted',Path('.build/cp410-implementation/filesystem/nift').resolve()),('canonical-assignment',p/'prototype/nift')]:
 cg=p/f'{label}.callgrind';q=subprocess.run(['valgrind','--tool=callgrind','--log-file='+str(p/f'{label}-callgrind.log'),'--callgrind-out-file='+str(cg),str(binary),'-'],input=source,text=True,capture_output=True,check=True);rows.append(dict(label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),stdout=q.stdout,stderr=q.stderr));print(label,rows[-1]['instructions'],flush=True)
assert rows[0]['stdout']==rows[1]['stdout'] and rows[0]['stderr']==rows[1]['stderr'];(p/'instructions.json').write_text(json.dumps(rows,indent=2)+'\n')
