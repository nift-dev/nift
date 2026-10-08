import pathlib,subprocess,resource,time,statistics,json,sys
b=pathlib.Path('.build/cp410');root=b/'cpu-recheck';root.mkdir(exist_ok=True);names=['call-scalar'];cases=[]
for name in names:
 n=160000
 if name=='loops':s=f's := 0\ni := 0\nwhile(i < {n}) {{ s += i; i += 1 }}\nprint(s)\n';expected=str(n*(n-1)//2)
 elif name=='call-scalar':s=f'fn(f(x)) {{ return x + 1 }}\ns := 0\ni := 0\nwhile(i < {n}) {{ s += f(i); i += 1 }}\nprint(s)\n';expected=str(n*(n+1)//2)
 elif name=='call-noarg':s=f'fn(f()) {{ return 1 }}\ns := 0\ni := 0\nwhile(i < {n}) {{ s += f(); i += 1 }}\nprint(s)\n';expected=str(n)
 else:s=f'a := []\ni := 0\nwhile(i < {n}) {{ a.push((i*7919) % {n}); i += 1 }}\nb := a.sort_by(x => '+('x' if name=='sort-identity' else 'a[x]')+')\nprint(b.size())\n';expected=str(n)
 p=root/(name+'.f');p.write_text(s);cases.append((name,p,expected))
bins=[str(pathlib.Path(p).resolve()) for p in sys.argv[2:4]];result=[]
for name,p,expected in cases:
 samples=[[],[]]
 for rep in range(30):
  for i in ([0,1] if rep%2==0 else [1,0]):
   before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();q=subprocess.run([bins[i],str(p)],capture_output=True,text=True,check=True);after=resource.getrusage(resource.RUSAGE_CHILDREN);assert q.stdout.strip()==expected
   if rep>2:samples[i].append(dict(cpu_ms=1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime),wall_ms=1000*(time.perf_counter()-t)))
 med=[statistics.median(x['cpu_ms'] for x in s) for s in samples];r=dict(name=name,samples=samples,cpu_median_ms=med,cpu_percent=100*(med[1]/med[0]-1));result.append(r);(root/(sys.argv[1]+'.json')).write_text(json.dumps(result,indent=2)+'\n');print(name,med,r['cpu_percent'],flush=True)
