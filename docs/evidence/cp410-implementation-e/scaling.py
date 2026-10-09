from pathlib import Path
import subprocess,json,re,hashlib
p=Path(__file__).resolve().parent;rows=[]
for size in [8192,16384,32768,65536,131072]:
 wrapper='s="".to_upper().replace("A","xy")';payload='a'*(size-len(wrapper));expression='s="'+payload+'".to_upper().replace("A","xy")';assert len(expression.encode())==size
 source='s:="";i:=0;while(i<128){'+expression+';i+=1};print(s)'
 for label,binary in [('accepted',Path('.build/cp410-implementation/filesystem/nift').resolve()),('canonical-assignment',p/'prototype/nift')]:
  cg=p/f'scaling-{size}-{label}.callgrind';log=p/f'scaling-{size}-{label}.log';q=subprocess.run(['valgrind','--tool=callgrind','--log-file='+str(log),'--callgrind-out-file='+str(cg),str(binary),'-'],input=source,text=True,capture_output=True,check=True);assert q.stdout=='xy'*len(payload)+'\n' and not q.stderr,(size,label,q.stderr)
  rows.append(dict(expression_bytes=size,iterations=128,label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),output_bytes=len(q.stdout.encode()),source_sha256=hashlib.sha256(source.encode()).hexdigest()));(p/'scaling-instructions.json').write_text(json.dumps(rows,indent=2)+'\n')
 print('PASS',size,rows[-2]['instructions']/rows[-1]['instructions'],flush=True)
