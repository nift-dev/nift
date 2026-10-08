"""Longer controls to distinguish steady execution from fixed CLI startup."""
import pathlib,json,resource,subprocess,time,statistics
b=pathlib.Path('.build/cp49-wave2/long-cpu');b.mkdir(parents=True,exist_ok=True)
variants=['start-nift','path2-nift','sort-nift','alias-guard-nift','location-scratch-nift','final-nift'];bins=[str(pathlib.Path('.build/cp49-wave2',x).resolve()) for x in variants];results=[]
for name in ['loops','call-noarg','call-scalar','call-multi','call-recursion','call-closure','call-callback','sort-aggregate','sort-multi']:
 p=b/(name+'.f')
 if name.startswith('sort-'):
  s=pathlib.Path('.build/cp49-wave2/probes',name+'-2000.f').read_text();p.write_text(s.replace('records-2000.json','records-16000.json'));n=16000
 else:
  s=pathlib.Path('.build/cp49/probes',name+'-2000.f').read_text();assert 'while(i < 2000)' in s;p.write_text(s.replace('while(i < 2000)','while(i < 50000)'));n=50000
 expected=None;item={'name':name,'n':n,'variants':[{'binary':v,'samples':[]} for v in variants]}
 for rep in range(15):
  for i in [(rep+k)%len(bins) for k in range(len(bins))]:
   before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();q=subprocess.run([bins[i],str(p.resolve())],capture_output=True,text=True);wall=time.perf_counter()-t;after=resource.getrusage(resource.RUSAGE_CHILDREN);assert q.returncode==0,q.stderr;output='\n'.join(q.stdout.splitlines()[:-1]);expected=output if expected is None else expected;assert expected==output
   if rep:item['variants'][i]['samples'].append({'cpu_ms':1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime),'wall_ms':1000*wall,'in_process_ms':float(q.stdout.splitlines()[-1])})
 for v in item['variants']:
  for metric in ['cpu_ms','wall_ms','in_process_ms']:v['median_'+metric]=statistics.median(x[metric] for x in v['samples'])
 results.append(item);(b/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(name,[round(v['median_cpu_ms'],2) for v in item['variants']],flush=True)
