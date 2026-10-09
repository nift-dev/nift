import pathlib,tempfile,json,subprocess,time,resource
p=pathlib.Path(tempfile.mkdtemp(prefix='nift-pipeline-10k-'));(p/'.nift').mkdir();(p/'content').mkdir();(p/'public').mkdir()
(p/'.nift/config.json').write_text(json.dumps({'config':{'content-dir':'content/','content-ext':'.html','output-dir':'public/','output-ext':'.html','default-template':'','build-threads':4,'incremental-mode':'modified','minify-exts':[]}}))
(p/'.nift/tracked.json').write_text(json.dumps({'tracked':[{'name':f'p{i}','title':f'Page {i}'} for i in range(10000)]}))
for i in range(10000):(p/'content'/f'p{i}.html').write_text('plain\n')
b=pathlib.Path('.build/cp410-pipeline/nift-before').resolve();rows=[]
for r in range(3):
 for args in [['build','--all'],['build'],['status']]:
  t=time.monotonic();c=resource.getrusage(resource.RUSAGE_CHILDREN);v=subprocess.run([str(b),*args],cwd=p,capture_output=True);a=resource.getrusage(resource.RUSAGE_CHILDREN);assert v.returncode==0,v.stderr;rows.append({'args':args,'wall':time.monotonic()-t,'cpu':a.ru_utime+a.ru_stime-c.ru_utime-c.ru_stime})
pathlib.Path('.build/cp410-pipeline/baseline.json').write_text(json.dumps({'root':str(p),'rows':rows},indent=2));print('PASS 10k baseline',str(p),flush=True)
