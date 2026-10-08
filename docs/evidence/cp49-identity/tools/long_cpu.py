"""Quiescent rotating native family controls, after all instrumented jobs finish."""
import pathlib,json,subprocess,resource,time,statistics
b=pathlib.Path('.build/cp49-identity'); d=b/'long';d.mkdir(exist_ok=True)
rows=json.loads((b/'probes.json').read_text());rows.append(next(r for r in json.load(open('.build/cp49/probes.json')) if r['name']=='loops' and r['n']==2000));binary=str(pathlib.Path('nift').resolve());oracle=str(pathlib.Path('.build/cp49-wave2/start-nift').resolve());items=[]
for r in rows:
 if r['name']=='sort-unused-50':continue
 name=r['name'];p=d/(name+'.f')
 if name.startswith('call-') or name=='loops':
  s=pathlib.Path('.build/cp49-wave2/long-cpu',name+'.f').read_text();n=50000
 elif name=='sort-aggregate':
  s=pathlib.Path(r['path']).read_text().replace('records-2000.json','records-16000.json');n=16000
 else:s=pathlib.Path(r['path']).read_text().replace('2000','16000');n=16000
 p.write_text(s);q=subprocess.run([oracle,str(p.resolve())],capture_output=True,text=True,check=True);expected='\n'.join(q.stdout.splitlines()[:-1]);items.append(dict(name=name,n=n,path=str(p.resolve()),expected=expected,samples=[]))
for rep in range(16):
 for i in [(rep+k)%len(items) for k in range(len(items))]:
  item=items[i];before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();q=subprocess.run([binary,item['path']],capture_output=True,text=True,check=True);wall=time.perf_counter()-t;after=resource.getrusage(resource.RUSAGE_CHILDREN);assert '\n'.join(q.stdout.splitlines()[:-1])==item['expected']
  if rep:item['samples'].append(dict(cpu_ms=1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime),wall_ms=wall*1000,in_process_ms=float(q.stdout.splitlines()[-1])))
for item in items:
 for metric in ['cpu_ms','wall_ms','in_process_ms']:item['median_'+metric]=statistics.median(s[metric] for s in item['samples'])
 print(item['name'],item['n'],round(item['median_cpu_ms'],2),flush=True)
(d/'results.json').write_text(json.dumps(items,indent=2)+'\n')
