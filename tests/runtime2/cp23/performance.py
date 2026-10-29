#!/usr/bin/env python3
"""Local CP23 diagnostic only. Interleaved matched sequential/website runs,
fixed total CPU work at 1/2/4/8 worker permits, raw samples and paired bootstrap
intervals. Does not grant reference-host or release performance acceptance."""
import argparse,hashlib,json,os,pathlib,platform,random,statistics,subprocess,tempfile,time
p=argparse.ArgumentParser();p.add_argument('--baseline',required=True);p.add_argument('--candidate',required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--samples',type=int,default=15);p.add_argument('--confirm-sequential',action='store_true');a=p.parse_args()
a.baseline=str(pathlib.Path(a.baseline).resolve());a.candidate=str(pathlib.Path(a.candidate).resolve())
rng=random.Random(2301)
def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
report={'diagnostic_only':True,'binaries':{k:{'path':getattr(a,k),'sha256':sha(getattr(a,k))} for k in ['baseline','candidate']},'platform':platform.platform(),'affinity':sorted(os.sched_getaffinity(0)),'cpuinfo':pathlib.Path('/proc/cpuinfo').read_text(),'sequential':{},'scaling':{}}
def save():a.output.write_text(json.dumps(report,indent=2)+'\n')
def summary(values):
 med=statistics.median(values);boot=sorted(statistics.median(rng.choices(values,k=len(values))) for _ in range(3000));return {'median':med,'ci95':[boot[75],boot[2924]],'samples':values}
with tempfile.TemporaryDirectory(prefix='cp23-performance-') as temp:
 root=pathlib.Path(temp)
 def measure(binary,source=None,workers=1,site=None):
  env=dict(os.environ,NIFT_VM='require',NIFT_VM_PARALLEL_WORKERS=str(workers),NIFT_VM_STATS=str(root/'stats.jsonl'))
  stats=root/'stats.jsonl';stats.unlink(missing_ok=True)
  if source is not None:(root/'p.f').write_text(source);cmd=[binary,'p.f'];cwd=root
  else:cmd=[binary,'build','--all'];cwd=site;env.pop('NIFT_VM_STATS');env['NIFT_VM']='1'
  started=time.perf_counter();p=subprocess.run(['/usr/bin/time','-f','%U %S %M','-o',str(root/'usage'),*cmd],cwd=cwd,env=env,capture_output=True,timeout=120);elapsed=time.perf_counter()-started
  if p.returncode:raise RuntimeError((cmd,p.returncode,p.stdout,p.stderr))
  st=json.loads(stats.read_text().splitlines()[-1]) if source is not None else None
  if st and (st.get('vm_executions')!=1 or st.get('legacy_executions')!=0):raise RuntimeError(st)
  user,system,rss=(root/'usage').read_text().split()
  return {'wall':elapsed,'user':float(user),'system':float(system),'rss_kib':int(rss),'stdout':p.stdout.decode(),'stats':st}
 workloads={
  'arithmetic':'i:=0;s:=0;while(i<1000000){s+=i;i+=1};print(s)',
  'captured-cell':'fn(make()){x:=0;return ()=>{x+=1;return x}};f:=make();i:=0;while(i<100000){f();i+=1};print(f())',
  'objects':'fn(f(x)){o:={"x":x,"y":1};return o.x+o.y};i:=0;s:=0;while(i<100000){s+=f(i);i+=1};print(s)',
  'collection':'a:=[1,2,3,4,5,6,7,8];i:=0;while(i<20000){b:=a.map((x)=>x+1);i+=1};print(i)'}
 if a.confirm_sequential:
  workloads={k:v.replace('1000000','30000000').replace('100000','3000000').replace('20000','600000') for k,v in workloads.items()}
  report['confirmation']='One predeclared longer sequential confirmation; initial 10–20 ms samples retained separately.'
 for name,source in workloads.items():
  rows=[];expected=None
  for round in range(a.samples+2):
   pair={}
   for kind in (['baseline','candidate'] if round%2==0 else ['candidate','baseline']):
    row=measure(getattr(a,kind),source)
    if expected is None:expected=row['stdout']
    if row['stdout']!=expected:raise RuntimeError('output drift '+name)
    pair[kind]=row
   if round>=2:rows.append(pair)
  ratios=[r['candidate']['wall']/r['baseline']['wall'] for r in rows]
  report['sequential'][name]={'source':source,'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'runs':rows,'ratio':summary(ratios),'target':1.03,'diagnostic_pass':statistics.median(ratios)<=1.03};save();print(name,report['sequential'][name]['ratio']['median'],flush=True)
 if a.confirm_sequential:
  save();raise SystemExit(0)
 n=1000000
 source=f'fn[async](f(x)){{i:=0;s:=x;while(i<{n}){{s+=i;i+=1}};return s}};'+''.join(f'p{i}:=f({i});' for i in range(8))+'print('+ '+'.join(f'(await p{i})' for i in range(8))+')'
 report['scaling_source']=source;rounds=[]
 for round in range(a.samples+2):
  data={}
  for workers in ([1,2,4,8] if round%2==0 else [8,4,2,1]):
   row=measure(a.candidate,source,workers)
   if row['stdout']!=str(8*n*(n-1)//2+28)+'\n':raise RuntimeError('scaling output')
   data[str(workers)]=row
  if round>=2:rounds.append(data)
 report['scaling_runs']=rounds
 for workers,target in [(2,1.4),(4,2),(8,3)]:
  ratios=[r['1']['wall']/r[str(workers)]['wall'] for r in rounds];report['scaling'][str(workers)]={'speedup':summary(ratios),'target':target,'diagnostic_pass':statistics.median(ratios)>=target}
 save();print('scaling',[(k,v['speedup']['median']) for k,v in report['scaling'].items()],flush=True)
 report['micro']={}
 micros={
  'task-startup':'fn[async](f()){return 1};print(await f())',
  'pool-reuse-100':'fn[async](f()){return 1};i:=0;while(i<100){p:=f();await p;i+=1};print(i)',
  'transfer-1000-values':'a:=[];i:=0;while(i<1000){a.push(i);i+=1};fn[async](f(xs)){return xs.size()};print(await f(a))',
  'rendezvous-1000':'c:=channel(0);fn[async](p()){i:=0;while(i<1000){send(c,i);i+=1};return i};a:=p();i:=0;while(i<1000){recv(c);i+=1};print(await a)',
  'cancel-shutdown':'c:=channel(0);fn[async](p()){recv(c);return 1};a:=p();sleep(10);cancel(a);try{await a}catch(e){print(e.code)}'}
 for name,text in micros.items():
  runs=[measure(a.candidate,text) for _ in range(7)]
  report['micro'][name]={'source':text,'runs':runs,'wall':summary([r['wall'] for r in runs])}
 save()
 # Real full website rewrites on independent matched directories.
 sites={}
 for kind in ['baseline','candidate']:
  site=root/kind;sites[kind]=site
  for d in ['.nift','content','templates','public']:(site/d).mkdir(parents=True,exist_ok=True)
  (site/'.nift/config.json').write_text(json.dumps({'config':{'content-dir':'content/','content-ext':'.html','output-dir':'public/','output-ext':'.html','default-template':'templates/template.html','build-threads':1,'incremental-mode':'modified','minify-exts':[]}}))
  (site/'.nift/tracked.json').write_text(json.dumps({'tracked':[{'name':f'p{i}','title':f'P{i}','template':'templates/template.html'} for i in range(1000)]}))
  for i in range(1000):(site/'content'/f'p{i}.html').write_text(f'<p>{i}</p>\n')
 rows=[]
 for round in range(a.samples+2):
  pair={}
  for kind in (['baseline','candidate'] if round%2==0 else ['candidate','baseline']):
   (sites[kind]/'templates/template.html').write_text(f'<main class="v{round%2}">@content</main>\n')
   pair[kind]=measure(getattr(a,kind),site=sites[kind])
   outputs=sorted((sites[kind]/'public').glob('*.html'))
   if len(outputs)!=1000 or any(f'class="v{round%2}"' not in f.read_text() for f in outputs):raise RuntimeError('incomplete website rewrite')
  if round>=2:rows.append(pair)
 base={f.name:sha(f) for f in (sites['baseline']/'public').glob('*.html')};current={f.name:sha(f) for f in (sites['candidate']/'public').glob('*.html')}
 if base!=current:raise RuntimeError('website output drift')
 ratios=[r['candidate']['wall']/r['baseline']['wall'] for r in rows]
 report['website']={'pages':1000,'threads':1,'runs':rows,'output_hashes':base,'ratio':summary(ratios),'target':1.03,'diagnostic_pass':statistics.median(ratios)<=1.03};save();print('website',report['website']['ratio']['median'],flush=True)
