"""Quiescent, rotating paired measurements for three exact saved binaries."""
import json,pathlib,subprocess,re,resource,time,statistics,hashlib
root=pathlib.Path('.build/cp49-wave2/final');root.mkdir(parents=True,exist_ok=True)
bins=[str(pathlib.Path(x).resolve()) for x in ['.build/cp49-campaign/start-nift','.build/cp49-wave2/start-nift','.build/cp49-wave2/final-nift']]
rows=json.load(open('.build/cp49/probes.json'))+json.load(open('.build/cp49-campaign/extra-probes.json'))+json.load(open('.build/cp49-wave2/probes.json'));results=[]
for r in rows:
 if r['n'] not in [0,2000]:continue
 item={'name':r['name'],'n':r['n'],'variants':[]}
 def oracle(p):
  assert p.returncode==0,p.stderr
  assert '\n'.join(p.stdout.splitlines()[:-1])==r['expected'],(r['name'],p.stdout)
 for i,binary in enumerate(bins):
  log=root/f"{r['name']}-{i}.callgrind";p=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(log),binary,r['path']],capture_output=True,text=True,timeout=300);oracle(p)
  instr=int(re.search(r'^summary: (\d+)',log.read_text(),re.M)[1])
  p=subprocess.run(['valgrind','--error-exitcode=97','--leak-check=full',binary,r['path']],capture_output=True,text=True,timeout=300);oracle(p);(root/f"{r['name']}-{i}.memcheck").write_text(p.stderr);assert 'ERROR SUMMARY: 0 errors' in p.stderr
  m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',p.stderr);assert m
  v={'binary_sha256':hashlib.sha256(pathlib.Path(binary).read_bytes()).hexdigest(),'instructions':instr,'allocations':int(m[1].replace(',','')),'frees':int(m[2].replace(',','')),'bytes':int(m[3].replace(',','')),'errors':0,'rss_kib':[],'samples':[]}
  assert v['allocations']==v['frees']
  for _ in range(5):
   p=subprocess.run(['/usr/bin/time','-f','%M',binary,r['path']],capture_output=True,text=True);oracle(p);v['rss_kib'].append(int(p.stderr.strip()))
  item['variants'].append(v)
 for rep in range(8):
  for i in [(rep+k)%3 for k in range(3)]:
   before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();p=subprocess.run([bins[i],r['path']],capture_output=True,text=True);wall=time.perf_counter()-t;after=resource.getrusage(resource.RUSAGE_CHILDREN);oracle(p)
   if rep:item['variants'][i]['samples'].append({'cpu_ms':1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime),'wall_ms':1000*wall,'in_process_ms':float(p.stdout.splitlines()[-1])})
 for v in item['variants']:
  for metric in ['cpu_ms','wall_ms','in_process_ms']:v['median_'+metric]=statistics.median(x[metric] for x in v['samples'])
 results.append(item);(root/'three-state.json').write_text(json.dumps(results,indent=2)+'\n');print(r['name'],[v['instructions'] for v in item['variants']],[v['allocations'] for v in item['variants']],flush=True)
