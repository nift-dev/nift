import pathlib,tempfile,json,subprocess,time,resource,re
binary=pathlib.Path('nift').resolve();rows=[];n=10000
with tempfile.TemporaryDirectory(prefix='nift-dag-10k-') as temp:
 p=pathlib.Path(temp);(p/'.nift').mkdir();(p/'content').mkdir();(p/'public').mkdir();(p/'.nift/config.json').write_text(json.dumps({'config':{'content-dir':'content/','content-ext':'.html','output-dir':'public/','output-ext':'.html','default-template':'','build-threads':4,'incremental-mode':'modified','minify-exts':[]}}))
 for i in range(n):(p/'content'/f'p{i}.html').write_text('x')
 for shape in ['independent','chain','star','tree','fan-in','layered','sparse']:
  entries=[]
  for i in range(n):
   deps=[]
   if shape=='chain' and i:deps=[f'p{i-1}']
   elif shape=='star' and i:deps=['p0']
   elif shape=='tree' and i:deps=[f'p{(i-1)//2}']
   elif shape=='fan-in' and i==n-1:deps=[f'p{j}' for j in range(n-1)]
   elif shape=='layered' and i>=100:deps=[f'p{j}' for j in range((i//100-1)*100,(i//100)*100)]
   elif shape=='sparse' and i%100==1:deps=[f'p{i-1}']
   entry={'name':f'p{i}','title':'P'}
   if deps:entry['depends']=deps
   entries.append(entry)
  (p/'.nift/tracked.json').write_text(json.dumps({'tracked':entries}));old=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic();rss_file=p/'peak-rss.txt';r=subprocess.run(['/usr/bin/time','-f','%M','-o',str(rss_file),str(binary),'build','--all'],cwd=p,env=dict(__import__('os').environ,NIFT_TEST_BUILD_DAG_STATS='1'),capture_output=True,text=True,timeout=120);new=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.returncode==0,(shape,r.stderr);stats={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',r.stderr)};rows.append({'shape':shape,'items':n,'edges':sum(len(e.get('depends',[])) for e in entries),'peak_rss_kib':int(rss_file.read_text()),'wall':time.monotonic()-start,'cpu':new.ru_utime+new.ru_stime-old.ru_utime-old.ru_stime,'scheduler':stats,'queue_samples':[[int(v) for v in sample.split(':')] for sample in next((line.split()[1:] for line in r.stderr.splitlines() if line.startswith('BUILD_DAG_QUEUE ')),[])]});print(rows[-1],flush=True)
pathlib.Path('.build/cp410-pipeline/dag-performance.json').write_text(json.dumps(rows,indent=2))
