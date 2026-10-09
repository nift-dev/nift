import json,subprocess,re,pathlib,resource,time,statistics
b=pathlib.Path('.build/cp410-followup');out=b/'object-metrics';out.mkdir(exist_ok=True);before=(b/'accepted-nift').resolve();after=pathlib.Path('nift').resolve();rows=[]
for width in [8,32,128,512,2000,8000]:
 for position,index in [('first',0),('middle',width//2),('last',width-1),('missing',width)]:
  path=out/f'object-{width}-{position}.f';literal=json.dumps({f'k{i}':i for i in range(width)},separators=(',',':'))
  source=f'o := {literal}\ni := 0\ntotal := 0\nwhile(i < 2000) {{ total += o["k{index}"]; i += 1 }}\nprint(total)\n';path.write_text(source)
  rows.append(dict(name=f'object-{width}-{position}',path=str(path.resolve()),expected='' if position=='missing' else str(index*2000)+'\n',code=1 if position=='missing' else 0,width=width,position=position))
controls=json.loads(pathlib.Path('.build/cp410/probes.json').read_text())
for r in controls:
 if r['n']==2000 and (r['name'] in ['loops','call-noarg','call-scalar','array-build','captured-sort','unused-captures','map-scalar'] or any(k in r['name'] for k in ['bfs','json','frequency','collection','official'])):
  rows.append(dict(name='control-'+r['name'],path=r['path'],expected=r['expected']+'\n',code=0))
metrics=[]
for r in rows:
 pair=[]
 for which,binary in [('before',before),('after',after)]:
  cg=out/(r['name']+'-'+which+'.callgrind');q=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(binary),r['path']],capture_output=True,text=True,timeout=600);assert q.returncode==r['code'] and q.stdout==r['expected'],(r,q)
  instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]);q=subprocess.run(['valgrind','--error-exitcode=97','--leak-check=full',str(binary),r['path']],capture_output=True,text=True,timeout=600);(out/(r['name']+'-'+which+'.memcheck')).write_text(q.stderr);assert q.returncode==r['code'] and 'ERROR SUMMARY: 0 errors' in q.stderr
  m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',q.stderr);q=subprocess.run(['/usr/bin/time','-v',str(binary),r['path']],capture_output=True,text=True);rss=int(re.search(r'Maximum resident set size \(kbytes\): (\d+)',q.stderr)[1]);pair.append(dict(instructions=instructions,allocations=int(m[1].replace(',','')),bytes=int(m[3].replace(',','')),rss_kib=rss))
 samples=[[],[]]
 for rep in range(15):
  for i in ([0,1] if rep%2==0 else [1,0]):
   usage=resource.getrusage(resource.RUSAGE_CHILDREN);q=subprocess.run([str([before,after][i]),r['path']],capture_output=True,text=True);used=resource.getrusage(resource.RUSAGE_CHILDREN);assert q.returncode==r['code'] and q.stdout==r['expected']
   if rep:samples[i].append(1000*(used.ru_utime+used.ru_stime-usage.ru_utime-usage.ru_stime))
 for i in [0,1]:pair[i].update(cpu_median_ms=statistics.median(samples[i]),cpu_samples_ms=samples[i])
 metrics.append(dict(**r,before=pair[0],after=pair[1],instruction_percent=100*(pair[1]['instructions']/pair[0]['instructions']-1)));(out/'metrics.json').write_text(json.dumps(metrics,indent=2)+'\n');print(r['name'],round(metrics[-1]['instruction_percent'],2),flush=True)
print('COMPLETE',len(metrics),flush=True)
