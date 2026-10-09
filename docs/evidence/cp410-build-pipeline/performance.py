import pathlib,subprocess,json,time,resource,statistics,os
root=pathlib.Path(json.loads(pathlib.Path('.build/cp410-pipeline/baseline.json').read_text())['root']);bins={'before':pathlib.Path('.build/cp410-pipeline/nift-before').resolve(),'after':pathlib.Path('nift').resolve()};rows=[]
for pair in range(6):
 for label in (['before','after'] if pair%2==0 else ['after','before']):
  for args in [['build','--all'],['build'],['status']]:
   old=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic();r=subprocess.run([str(bins[label]),*args],cwd=root,capture_output=True);new=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.returncode==0,(label,args,r.stderr);rows.append({'label':label,'args':args,'cpu':new.ru_utime+new.ru_stime-old.ru_utime-old.ru_stime,'wall':time.monotonic()-start})
summary={str(args):{label:{key:statistics.median(r[key] for r in rows if r['label']==label and r['args']==args) for key in ['cpu','wall']} for label in bins} for args in [['build','--all'],['build'],['status']]}
r=subprocess.run([str(bins['after']),'build','--all'],cwd=root,capture_output=True,text=True,env=dict(os.environ,NIFT_TEST_BUILD_DAG_STATS='1'));assert r.returncode==0;assert 'BUILD_DAG nodes=0 edges=0' in r.stderr
pathlib.Path('.build/cp410-pipeline/performance.json').write_text(json.dumps({'rows':rows,'summary':summary,'no_dep_guard':r.stderr},indent=2));print(json.dumps(summary),flush=True)
