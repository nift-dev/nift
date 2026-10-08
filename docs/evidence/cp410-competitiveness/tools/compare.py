import json,subprocess,re,pathlib,resource,time,statistics,sys
b=pathlib.Path('.build/cp410');label=sys.argv[1];baseline=pathlib.Path(sys.argv[2]).resolve();candidate=pathlib.Path(sys.argv[3]).resolve();out=b/label;out.mkdir(exist_ok=True);rows=json.loads((b/'probes.json').read_text());chosen=[r for r in rows if r['n'] in [0,2000] or r['name'].startswith('official') or r['name'] in ['captured-sort','unused-captures']];metrics=[]
for r in chosen:
 key=r['name']+'-'+str(r['n']);pair=[]
 for which,binary in [('before',baseline),('after',candidate)]:
  profile=out/(key+'-'+which+'.callgrind');q=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(profile),str(binary),r['path']],text=True,capture_output=True,timeout=600);assert q.returncode==0 and q.stdout.strip()==r['expected'],(key,which,q.stdout,q.stderr);instructions=int(re.search(r'^summary: (\d+)',profile.read_text(),re.M)[1]);q=subprocess.run(['valgrind','--error-exitcode=97','--leak-check=full',str(binary),r['path']],text=True,capture_output=True,timeout=600);(out/(key+'-'+which+'.memcheck')).write_text(q.stderr);assert q.returncode==0 and 'ERROR SUMMARY: 0 errors' in q.stderr,(key,which,q.stderr);m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',q.stderr);assert m
  q=subprocess.run(['/usr/bin/time','-v',str(binary),r['path']],text=True,capture_output=True,check=True);(out/(key+'-'+which+'.time-v')).write_text(q.stderr);rss=int(re.search(r'Maximum resident set size \(kbytes\): (\d+)',q.stderr)[1]);pair.append(dict(instructions=instructions,allocations=int(m[1].replace(',','')),bytes=int(m[3].replace(',','')),rss_kib=rss))
 samples=[[],[]]
 for rep in range(10):
  for index in ([0,1] if rep%2==0 else [1,0]):
   binary=[baseline,candidate][index];usage=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();q=subprocess.run([str(binary),r['path']],text=True,capture_output=True,check=True);after=resource.getrusage(resource.RUSAGE_CHILDREN);assert q.stdout.strip()==r['expected']
   if rep>0:samples[index].append(dict(cpu_ms=1000*(after.ru_utime+after.ru_stime-usage.ru_utime-usage.ru_stime),wall_ms=1000*(time.perf_counter()-t)))
 for i in [0,1]:pair[i].update(cpu_median_ms=statistics.median(x['cpu_ms'] for x in samples[i]),wall_median_ms=statistics.median(x['wall_ms'] for x in samples[i]),samples=samples[i])
 item=dict(name=r['name'],n=r['n'],before=pair[0],after=pair[1],instruction_percent=100*(pair[1]['instructions']/pair[0]['instructions']-1),allocations_saved=pair[0]['allocations']-pair[1]['allocations'],bytes_saved=pair[0]['bytes']-pair[1]['bytes']);metrics.append(item);(out/'metrics.json').write_text(json.dumps(metrics,indent=2)+'\n');print(key,'Ir%',round(item['instruction_percent'],3),'alloc_saved',item['allocations_saved'],flush=True)
print('COMPLETE',len(metrics),'paired comparisons',flush=True)
