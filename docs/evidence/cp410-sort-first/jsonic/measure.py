from pathlib import Path
import subprocess,json,re,os,hashlib
p=Path('.build/cp410-followup/jsonic');include=Path('/home/nick/Repositories/nift/jsonic/jsonic/include');binary=str((p/'probe').resolve())
command=['g++','-std=c++17','-O2','-I'+str(include),str(p/'probe.cpp'),str(p/'Alloc.cpp'),'-o',binary];(p/'build.json').write_text(json.dumps(command)+'\n');subprocess.run(command,check=True);rows=[]
for width in [8,32,128,512,2000,8000]:
 for kind in ['unique','duplicate-early','duplicate-middle','duplicate-end','malformed']:
  entries=[json.dumps(f'k{i}')+':'+str(i) for i in range(width)]
  if kind=='duplicate-early':entries[2]='"k0":2'
  if kind=='duplicate-middle':entries[width//2]='"k0":1'
  if kind=='duplicate-end':entries[-1]='"k0":1'
  source='{'+','.join(entries)+'}'
  if kind=='malformed':source=source[:-1]
  path=p/f'{kind}-{width}.json';path.write_text(source)
  for policy in (['reject','preserve'] if kind=='unique' else ['reject']):
   output=p/f'{kind}-{width}-{policy}.callgrind'
   q=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+str(output),binary,str(path.resolve()),policy],env={**os.environ,'CP51_PHASE':'factory'},capture_output=True,text=True,check=True,timeout=600)
   ok=q.stdout.startswith('PASS');assert ok==(kind=='unique'),(kind,q.stdout)
   m=re.search(r'phase factory calls=(\d+) allocations=(\d+) bytes=(\d+)',q.stderr);assert m
   row=dict(width=width,kind=kind,policy=policy,source_sha256=hashlib.sha256(source.encode()).hexdigest(),stdout=q.stdout,instructions=int(re.search(r'^summary: (\d+)',output.read_text(),re.M)[1]),allocations=int(m[2]),allocated_bytes=int(m[3]),membership_comparisons=width*(width-1)//2 if kind=='unique' and policy=='reject' else 0 if policy=='preserve' else None)
   rows.append(row);(p/'metrics.json').write_text(json.dumps(rows,indent=2)+'\n');print('JSONIC',width,kind,policy,row['instructions'],row['allocations'],repr(q.stdout),flush=True)
print('PASS canonical Jsonic++ parse matrix',flush=True)
