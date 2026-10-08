import json,subprocess,re
from pathlib import Path
b=Path('.build/cp49-campaign/final');rows=json.load(open('.build/cp49/probes.json'))+json.load(open('.build/cp49-campaign/extra-probes.json'));out=[]
for r in rows:
 if r['n'] not in [0,2000]:continue
 item={'name':r['name'],'n':r['n'],'variants':[]}
 for label,binary in [('before','.build/cp49-campaign/start-nift'),('after','.build/cp49-campaign/final/nift')]:
  log=b/(r['name']+'-'+label+'-memory.log');p=subprocess.run(['valgrind','--error-exitcode=97','--leak-check=full',binary,r['path']],capture_output=True,text=True,timeout=300);log.write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr;assert re.search(r'ERROR SUMMARY: 0 errors',p.stderr),p.stderr;assert '\n'.join(p.stdout.splitlines()[:-1])==r['expected']
  m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',p.stderr);assert m
  rss=[]
  for _ in range(5):
   q=subprocess.run(['/usr/bin/time','-f','%M',binary,r['path']],capture_output=True,text=True,check=True);rss.append(int(q.stderr.strip()))
  item['variants'].append(dict(label=label,allocations=int(m[1].replace(',','')),frees=int(m[2].replace(',','')),bytes=int(m[3].replace(',','')),maxrss_kib=rss,errors=0))
 out.append(item);(b/'memory-all.json').write_text(json.dumps(out,indent=2)+'\n');print('MEMORY',r['name'],[v['allocations'] for v in item['variants']],flush=True)
