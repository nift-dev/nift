import json,subprocess,re
from pathlib import Path
b=Path(__file__).resolve().parents[4]/'.build/cp49';out=[]
for r in json.loads((b/'probes.json').read_text()):
 if r['n'] not in [0,2000]:continue
 log=b/'profiles'/(r['name']+'-memory.log')
 if not log.exists() or 'error:' in log.read_text():
  cmd=['valgrind','--tool=memcheck','--leak-check=no',str(Path('nift').resolve()),r['path']]
  p=subprocess.run(cmd,capture_output=True,text=True,timeout=180);log.write_text(repr(cmd)+'\n'+p.stdout+p.stderr);assert p.returncode==0,(r['name'],p.stderr)
 text=log.read_text();m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',text);assert m
 out.append(dict(name=r['name'],n=r['n'],allocations=int(m[1].replace(',','')),frees=int(m[2].replace(',','')),allocated_bytes=int(m[3].replace(',',''))));print('MEMORY',r['name'],flush=True)
(b/'allocations.json').write_text(json.dumps(out,indent=2)+'\n')
