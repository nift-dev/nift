from pathlib import Path
import json,sys
sys.path.insert(0,'/home/nick/Repositories/nift/shell-benchmark/scripts')
from workload_suite import cases,source,PAYLOAD,KEEP
p=Path('.build/cp410-shell');rows=[]
for op in ('traverse','create-small'):
 for n in (1,1000,10000,100000) if op=='create-small' else (1000,10000,100000):
  c=next(dict(x) for x in cases() if x['operation']==op);c['count']=n
  root=p/f'official-{op}-{n}';(root/'files').mkdir(parents=True,exist_ok=True);d=max(20000 if n==100000 else n//5,1);targets=[];keepers=[]
  for i in range(n+d):
   keep=(i%6==0 and len(keepers)<d) or len(targets)==n
   rel=f'files/obj-{i:06d}-'+('keep' if keep else 'target')+'.dat';(keepers if keep else targets).append(rel)
   if keep or op=='traverse':(root/rel).write_bytes((KEEP if keep else PAYLOAD).encode())
  assert len(targets)==n and len(keepers)==d
  (root/'targets.txt').write_text('\n'.join(targets)+'\n');(root/'source.f').write_text(source(c,'nift'))
  rows.append({'operation':op,'n':n,'keepers':d,'code':source(c,'nift'),'target_name_length':len(Path(targets[0]).name),'keeper_name_length':len(Path(keepers[0]).name)})
Path('docs/evidence/cp410-shell/official-fixtures.json').write_text(json.dumps(rows,indent=2)+'\n');print('Prepared exact local official fixture shapes')
