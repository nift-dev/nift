"""Independent aggregate propagation and JSON shape probes; no official inputs."""
import json
from pathlib import Path
b=Path('.build/cp49-campaign/extra');b.mkdir(parents=True,exist_ok=True);rows=[]
for n in [2000,16000]:
 records=[{'v':i,'nested':[i,i+1,{'s':'record'}]} for i in range(n)]
 file=b/f'records-{n}.json';file.write_text(json.dumps(records,separators=(',',':')))
 for name,body in [('map-aggregate','b := a.map(x => x); print(b.length())'),('filter-aggregate','b := a.filter(x => true); print(b.length())'),('group-unique','b := a.group_by(x => x.v); print(b.size())'),('group-repeated','b := a.group_by(x => x.v % 16); print(b.size())')]:
  p=b/f'{name}-{n}.f';p.write_text('a := inject('+json.dumps(str(file.resolve()))+')\nprobe_timer := timer(); probe_timer.start()\n'+body+'\nprobe_timer.stop(); print(probe_timer.elapsed())\n');rows.append(dict(name=name,n=n,path=str(p.resolve()),expected=str(16 if name=="group-repeated" else n)))
Path('.build/cp49-campaign/extra-probes.json').write_text(json.dumps(rows,indent=2)+'\n')
# Wide objects, 48-level object chains, and heterogeneous nested arrays.
shapes=Path('.build/cp49-campaign/10');shapes.mkdir(exist_ok=True,parents=True)
for n in [2000,16000]:
 wide={f'k{i}':{'v':i,'mixed':[None,True,'s']} for i in range(n)}
 deep=[{'v':i} for i in range(n)]
 for _ in range(48):deep={'next':deep}
 mixed=[{'array':[1,{'x':i}],'s':'value'} for i in range(n)]
 for label,value in [('wide',wide),('deep',deep),('mixed',mixed)]:
  (shapes/f'{label}-{n}.json').write_text(json.dumps(value,separators=(',',':')))
