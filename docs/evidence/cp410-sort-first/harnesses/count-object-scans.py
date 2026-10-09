from pathlib import Path
import json,subprocess,re
b=Path('.build/cp410-followup');out=b/'object-stats';rows=[]
for width in [8,32,128,512,2000,8000]:
 for position,index in [('first',0),('middle',width//2),('last',width-1),('missing',width)]:
  path=out/'counter.f';prefix='o := '+json.dumps({f'k{i}':i for i in range(width)},separators=(',',':'))+'\n';pair=[]
  for label in ['legacy','candidate']:
   samples=[]
   for n in [0,100]:
    path.write_text(prefix+f'i := 0\ntotal := 0\nwhile(i < {n}) {{ total += o["k{index}"]; i += 1 }}\nprint(total)\n');q=subprocess.run([str((out/(label+'-nift')).resolve()),str(path.resolve())],capture_output=True,text=True,timeout=120);assert q.returncode==(1 if position=='missing' and n else 0);m=re.search(r'object-lookups scans=(\d+) comparisons=(\d+)\n$',q.stderr);assert m;samples.append(list(map(int,m.groups())))
   pair.append([x-y for x,y in zip(samples[1],samples[0])])
  expected=[1,width] if position=='missing' else [100,100*(index+1)];assert pair[1]==expected,(width,position,pair)
  assert pair[0]==(expected if position=='missing' else [2*x for x in expected]),(width,position,pair)
  rows.append(dict(width=width,position=position,before_scans=pair[0][0],before_comparisons=pair[0][1],after_scans=pair[1][0],after_comparisons=pair[1][1]));(out/'scan-matrix.json').write_text(json.dumps(rows,indent=2)+'\n');print(width,position,pair,flush=True)
print('PASS scan matrix',flush=True)
