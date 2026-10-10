import pathlib,json,subprocess,shutil,os,time,resource,statistics
p=pathlib.Path('/tmp/nift-path-experiment');os.sched_setaffinity(0,{0,1,2,3});bins={'experiment1':str(p/'nift-experiment1'),'experiment2':'/home/nick/Repositories/nift/nift/nift'};rows=[]
def measure(exe,d):
 old=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.perf_counter_ns();r=subprocess.run([exe,'build'],cwd=d,capture_output=True,text=True);wall=(time.perf_counter_ns()-start)/1e6;new=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.returncode==0,(r.stdout,r.stderr);return wall,(new.ru_utime+new.ru_stime-old.ru_utime-old.ru_stime)*1000
for n in [100,5000]:
 base=p/f'website-{n}';shutil.rmtree(base,ignore_errors=True)
 for name in ['.nift','content','public','templates','data']:(base/name).mkdir(parents=True,exist_ok=True)
 config={'content-dir':'content/','content-ext':'.html','output-dir':'public/','output-ext':'.html','default-template':'templates/main.html','build-threads':4,'incremental-mode':'modified','minify-exts':[]}
 (base/'.nift/config.json').write_text(json.dumps({'config':config}));(base/'.nift/tracked.json').write_text(json.dumps({'tracked':[dict(name=str(i),title=str(i),template='templates/main.html') for i in range(n)]}))
 (base/'templates/main.html').write_text('<html><body>@input("data/shared.html")<main>@content</main></body></html>\n');(base/'data/shared.html').write_text('<header>SHARED</header>\n')
 for i in range(n):(base/f'content/{i}.html').write_text(f'<h1>PAGE{i}</h1>\n')
 subprocess.run([bins['experiment1'],'build','--all'],cwd=base,check=True,stdout=subprocess.DEVNULL)
 expected={f'{i}.html':(base/f'public/{i}.html').read_bytes() for i in range(n)}
 cases=['noop'] if n==100 else ['noop','one-page','shared-template','shared-dependency']
 for case in cases:
  rounds=22 if case=='noop' else 3
  for round_ in range(rounds):
   maps=[]
   for label in (['experiment1','experiment2'] if round_%2==0 else ['experiment2','experiment1']):
    if case=='noop':d=base
    else:
     d=p/'website-working';shutil.rmtree(d,ignore_errors=True);shutil.copytree(base,d)
    before={i:(d/f'.nift/public/{i}.info.json').stat().st_mtime_ns for i in range(n)};oracle=dict(expected)
    if case=='one-page':
     (d/'content/0.html').write_text('<h1>EDIT0</h1>\n');oracle['0.html']=oracle['0.html'].replace(b'PAGE0',b'EDIT0')
    elif case=='shared-template':
     f=d/'templates/main.html';f.write_text(f.read_text()+'<!--TEMPLATE-EDIT-->\n');oracle={k:v+b'<!--TEMPLATE-EDIT-->\n' for k,v in oracle.items()}
    elif case=='shared-dependency':
     (d/'data/shared.html').write_text('<header>FANOUT</header>\n');oracle={k:v.replace(b'SHARED',b'FANOUT') for k,v in oracle.items()}
    wall,cpu=measure(bins[label],d);mapping={f'{i}.html':(d/f'public/{i}.html').read_bytes() for i in range(n)};assert mapping==oracle,(n,case,label,'output mismatch');count=sum((d/f'.nift/public/{i}.info.json').stat().st_mtime_ns!=before[i] for i in range(n));assert count==(0 if case=='noop' else 1 if case=='one-page' else n),(n,case,label,count);maps.append(mapping)
    rows.append({'size':n,'case':case,'round':round_,'warmup':case=='noop' and round_<2,'binary':label,'wall_ms':wall,'cpu_ms':cpu,'rebuilt_consumers':count,'output_oracle':True});(p/'websites.json').write_text(json.dumps(rows,indent=2)+'\n')
   assert maps[0]==maps[1]
  print(n,case,[(b,round(statistics.median(x['wall_ms'] for x in rows if x['size']==n and x['case']==case and x['binary']==b and not x['warmup']),3)) for b in bins],flush=True)
shutil.rmtree(p/'website-working',ignore_errors=True)
