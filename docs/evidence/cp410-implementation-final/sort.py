from pathlib import Path
import subprocess,json,re,hashlib
p=Path(__file__).resolve().parent;binary=p.parent/'save/nift';rows=[]
for width in [0,100]:
 for kind in ['identity','captured']:
  source=Path(f'.build/cp410-capture-frame/cases/n016000/u{width:03}/{kind:_<12}/official-equivalent.f').resolve();cg=p/f'sort-u{width}-{kind}.callgrind';log=p/f'sort-u{width}-{kind}.log'
  q=subprocess.run(['valgrind','--tool=callgrind','--log-file='+str(log),'--callgrind-out-file='+str(cg),str(binary),str(source)],capture_output=True,text=True,check=True)
  assert q.stdout=='1\n16000\n' and not q.stderr
  rows.append(dict(width=width,kind=kind,n=16000,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),stdout=q.stdout));(p/'sort.json').write_text(json.dumps(rows,indent=2)+'\n');print(rows[-1],flush=True)
