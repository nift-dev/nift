import json,subprocess,re
from pathlib import Path
b=Path(__file__).resolve().parents[4]/'.build/cp49';chosen=['sort-index','sort-arithmetic','sort-identity','call-scalar','call-closure','frequency','bfs','json-mutate','json-parse-convert','loops','filesystem'];out=[]
for r in json.loads((b/'probes.json').read_text()):
 if r['name'] not in chosen:continue
 name=r['name'];n=r['n'];p=b/'profiles'/f'{name}-{n}-scale.callgrind'
 if n==2000:p=b/'profiles'/f'{name}.callgrind'
 else:
  cmd=['valgrind','--tool=callgrind','--callgrind-out-file='+str(p),str(Path('nift').resolve()),r['path']];v=subprocess.run(cmd,capture_output=True,text=True,timeout=240);(b/'profiles'/f'{name}-{n}-scale.log').write_text(repr(cmd)+'\n'+v.stdout+v.stderr);assert v.returncode==0
 text=p.read_text();m=re.search(r'^summary: (\d+)',text,re.M);assert m,p;out.append(dict(name=name,n=n,instructions=int(m[1])));(b/'instruction-scaling.json').write_text(json.dumps(out,indent=2)+'\n');print('SCALE',name,n,m[1],flush=True)
