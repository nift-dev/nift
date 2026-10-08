import pathlib,json,subprocess,resource,time,statistics,sys
b=pathlib.Path('.build/cp410');root=b/'cpu-controls';root.mkdir(exist_ok=True);old=json.load(open('.build/cp49/probes.json'))+json.load(open('.build/cp49-campaign/extra-probes.json'));names=['sliding-window','filter-aggregate','loops','call-noarg','sort-identity','call-closure'];rows=[]
for name in names:
 rs=[r for r in old if r['name']==name and r['n']>=8000];r=max(rs,key=lambda x:x['n']);q=root/(name+'.f');q.write_text(pathlib.Path(r['path']).read_text().replace('probe_timer := timer(); probe_timer.start()','').replace('probe_timer.stop(); print(probe_timer.elapsed())',''));rows.append(dict(r,path=str(q.resolve())))
label=sys.argv[1];bins=[str(pathlib.Path(x).resolve()) for x in sys.argv[2:4]];output=[]
for r in rows:
 samples=[[],[]]
 for rep in range(36):
  for i in ([0,1] if rep%2==0 else [1,0]):
   usage=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.perf_counter();p=subprocess.run([bins[i],r['path']],capture_output=True,text=True,check=True);after=resource.getrusage(resource.RUSAGE_CHILDREN);assert p.stdout.strip()==r['expected'],(r['name'],p.stdout)
   if rep>4:samples[i].append(dict(cpu_ms=1000*(after.ru_utime+after.ru_stime-usage.ru_utime-usage.ru_stime),wall_ms=1000*(time.perf_counter()-start)))
 med=[statistics.median(x['cpu_ms'] for x in s) for s in samples];record=dict(name=r['name'],n=r['n'],samples=samples,cpu_median_ms=med,cpu_percent=100*(med[1]/med[0]-1));output.append(record);(root/(label+'.json')).write_text(json.dumps(output,indent=2)+'\n');print(r['name'],record['cpu_percent'],flush=True)
