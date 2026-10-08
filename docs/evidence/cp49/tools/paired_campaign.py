import json,subprocess,resource,time,statistics,sys,re
from pathlib import Path
out=Path(sys.argv[1]);out.mkdir(exist_ok=True,parents=True)
bins=[str(Path(x).resolve()) for x in sys.argv[2:4]]
names=sys.argv[4:];rows=json.load(open('.build/cp49/probes.json'));result=[]
for name in names:
 r=next(x for x in rows if x['name']==name and x['n']==(0 if name=='empty' else 2000));item={'name':name,'n':r['n'],'variants':[]}
 for i,b in enumerate(bins):
  log=out/(name+str(i)+'.callgrind');p=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(log),b,r['path']],capture_output=True,text=True,timeout=300);assert p.returncode==0,p.stderr
  assert '\n'.join(p.stdout.splitlines()[:-1])==r['expected']
  instr=int(re.search(r'^summary: (\d+)',log.read_text(),re.M)[1]);item['variants'].append({'instructions':instr,'samples':[]})
 for rep in range(8):
  for i in ([0,1] if rep%2==0 else [1,0]):
   before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();p=subprocess.run([bins[i],r['path']],capture_output=True,text=True);after=resource.getrusage(resource.RUSAGE_CHILDREN);assert p.returncode==0,p.stderr
   if rep:item['variants'][i]['samples'].append({'cpu_ms':1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime),'wall_ms':1000*(time.perf_counter()-t)})
 for v in item['variants']:
  for key in ['cpu_ms','wall_ms']:v['median_'+key]=statistics.median(x[key] for x in v['samples'])
 result.append(item);(out/'paired.json').write_text(json.dumps(result,indent=2)+'\n');a,b=item['variants'];print(name,'instructions',a['instructions'],b['instructions'],'CPU',round(a['median_cpu_ms'],2),round(b['median_cpu_ms'],2),flush=True)
