import json,subprocess,re,resource,time,statistics,pathlib,sys
b=pathlib.Path('.build/cp410');root=b/'profiles';root.mkdir(exist_ok=True);binary=str((b/'baseline/nift').resolve());rows=json.loads((b/'probes.json').read_text());result=json.loads((root/'metrics.json').read_text()) if (root/'metrics.json').exists() else []
selected=[r for r in rows if r['n'] in [0,2000] or r['name'].startswith('official') or r['name'] in ['captured-sort','unused-captures']]
for r in selected:
 if any(x['name']==r['name'] and x['n']==r['n'] for x in result):continue
 name=r['name']+'-'+str(r['n']);log=root/(name+'.callgrind');cmd=['valgrind','--tool=callgrind','--callgrind-out-file='+str(log),binary,r['path']];p=subprocess.run(cmd,capture_output=True,text=True,timeout=600);(root/(name+'.run.log')).write_text(p.stdout+p.stderr);assert p.returncode==0,(name,p.stderr);assert p.stdout.strip()==r['expected'],(name,p.stdout,r['expected']);count=int(re.search(r'^summary: (\d+)',log.read_text(),re.M)[1]);samples=[]
 for rep in range(10):
  before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();p=subprocess.run([binary,r['path']],capture_output=True,text=True,check=True);after=resource.getrusage(resource.RUSAGE_CHILDREN);assert p.stdout.strip()==r['expected'],name
  if rep:samples.append({'cpu_ms':1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime),'wall_ms':1000*(time.perf_counter()-t)})
 mem=root/(name+'.memcheck');p=subprocess.run(['valgrind','--error-exitcode=97','--leak-check=full','--xtree-memory=full','--xtree-memory-file='+str(root/(name+'.xtree')),binary,r['path']],capture_output=True,text=True,timeout=600);mem.write_text(p.stderr);assert p.returncode==0 and 'ERROR SUMMARY: 0 errors' in p.stderr,(name,p.stderr);m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',p.stderr);assert m
 rss=[]
 for _ in range(3):
  q=subprocess.run(['/usr/bin/time','-v',binary,r['path']],capture_output=True,text=True,check=True);(root/(name+'.time-v')).write_text(q.stderr);rss.append(int(re.search(r'Maximum resident set size \(kbytes\): (\d+)',q.stderr)[1]))
 for mode in ['self','inclusive']:
  q=subprocess.run(['callgrind_annotate','--threshold=95','--inclusive='+('yes' if mode=='inclusive' else 'no'),str(log)],capture_output=True,text=True,check=True);(root/(name+'-'+mode+'.txt')).write_text(q.stdout)
 item=dict(name=r['name'],n=r['n'],instructions=count,samples=samples,cpu_median_ms=statistics.median(x['cpu_ms'] for x in samples),wall_median_ms=statistics.median(x['wall_ms'] for x in samples),allocations=int(m[1].replace(',','')),frees=int(m[2].replace(',','')),bytes=int(m[3].replace(',','')),rss_kib=rss,errors=0);result.append(item);(root/'metrics.json').write_text(json.dumps(result,indent=2)+'\n');print(name,count,item['allocations'],flush=True)
print('COMPLETE fresh profiles',len(result),flush=True)
