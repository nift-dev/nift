import pathlib,json,statistics
p=pathlib.Path('/tmp/nift-path-experiment');r=json.load(open(p/'paired.json'));out=[]
for n in [100,1000,5000,10000]:
 for b in ['experiment1','experiment2']+(['original'] if n==10000 else []):
  s=[x for x in r if x['size']==n and x['binary']==b and not x['warmup']];assert len(s)==20
  out.append(dict(size=n,binary=b,n=len(s),median_ms=statistics.median(x['wall_ms'] for x in s),min_ms=min(x['wall_ms'] for x in s),max_ms=max(x['wall_ms'] for x in s),cpu_median_ms=statistics.median(x['cpu_ms'] for x in s)))
slopes={}
for b in ['experiment1','experiment2']:
 s=[x for x in out if x['binary']==b];xm=statistics.mean(x['size'] for x in s);ym=statistics.mean(x['median_ms'] for x in s);k=sum((x['size']-xm)*(x['median_ms']-ym) for x in s)/sum((x['size']-xm)**2 for x in s);slopes[b]=dict(ms_per_TU=k,intercept_ms=ym-k*xm)
(p/'analysis.json').write_text(json.dumps(dict(noop=out,slopes=slopes),indent=2)+'\n');print(json.dumps(dict(noop=out,slopes=slopes),indent=2))
