"""Independent aggregate propagation and JSON shape probes; no official inputs."""
import json
from pathlib import Path
b=Path('.build/cp49-campaign/extra');b.mkdir(parents=True,exist_ok=True);rows=[]
for n in [2000,16000]:
 records=[{'v':i,'nested':[i,i+1,{'s':'record'}]} for i in range(n)]
 file=b/f'records-{n}.json';file.write_text(json.dumps(records,separators=(',',':')))
 for name,body in [('map-aggregate','b := a.map(x => x); print(b.length())'),('filter-aggregate','b := a.filter(x => true); print(b.length())')]:
  p=b/f'{name}-{n}.f';p.write_text('a := inject('+json.dumps(str(file.resolve()))+')\nprobe_timer := timer(); probe_timer.start()\n'+body+'\nprobe_timer.stop(); print(probe_timer.elapsed())\n');rows.append(dict(name=name,n=n,path=str(p.resolve()),expected=str(n)))
Path('.build/cp49-campaign/extra-probes.json').write_text(json.dumps(rows,indent=2)+'\n')
