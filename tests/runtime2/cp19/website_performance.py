#!/usr/bin/env python3
"""Local ordinary website guard: balanced full rewrites with exact output checks.

Uses the established flat-page fixture shape, one build thread and two template
variants. Website rendering remains legacy; this is not VM coverage evidence.
"""
import argparse,hashlib,json,os,random,statistics,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--base',required=True);p.add_argument('--nift',required=True)
p.add_argument('--switch',required=True);p.add_argument('--out',type=Path,required=True)
p.add_argument('--pages',type=int,default=1000);p.add_argument('--samples',type=int,default=21)
a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
binaries={k:Path(v).resolve() for k,v in [('base',a.base),('default',a.nift),('switch',a.switch)]}
for name in binaries:
 root=a.out/name;root.mkdir();(root/'.nift').mkdir();(root/'content').mkdir();(root/'templates').mkdir()
 (root/'.nift/config.json').write_text(json.dumps({'config':{'content-dir':'content/','content-ext':'.html','output-dir':'public/','output-ext':'.html','default-template':'templates/page.html','build-threads':1,'incremental-mode':'modified','minify-exts':[]}}))
 (root/'.nift/tracked.json').write_text(json.dumps({'tracked':[{'name':f'p{i}','title':f'P{i}','template':'templates/page.html'} for i in range(a.pages)]}))
 for i in range(a.pages):(root/'content'/f'p{i}.html').write_text(f'<p>{i}</p>\n')
rows=[];rng=random.Random(191028)
for sample in range(-2,a.samples):
 order=list(binaries);rng.shuffle(order);variant=sample%2
 for name in order:
  root=a.out/name
  (root/'templates/page.html').write_text(f'<main class="v{variant}">@content</main>\n')
  env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=str(root),NO_COLOR='1',NIFT_VM='require')
  before=time.perf_counter_ns();r=subprocess.run([str(binaries[name]),'build','--all'],cwd=root,env=env,text=True,capture_output=True,timeout=120);elapsed=time.perf_counter_ns()-before
  (a.out/f'{name}-{sample}-stdout.txt').write_text(r.stdout);(a.out/f'{name}-{sample}-stderr.txt').write_text(r.stderr)
  assert r.returncode==0,(name,sample,r.returncode,r.stderr)
  outputs=sorted((root/'public').rglob('*.html'));assert len(outputs)==a.pages,(name,len(outputs))
  hashes={}
  for output in outputs:
   data=output.read_bytes();assert f'<main class="v{variant}">'.encode() in data,(name,output)
   hashes[str(output.relative_to(root/'public'))]=hashlib.sha256(data).hexdigest()
  if name==order[0]:expected=hashes
  else:assert hashes==expected,(name,sample,'output mismatch')
  rows.append(dict(binary=name,sample=sample,warmup=sample<0,variant=variant,elapsed_ns=elapsed,pages=len(outputs),output_tree_sha256=hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).hexdigest()))
  (a.out/'samples.json').write_text(json.dumps(rows,indent=2)+'\n')
summary=[]
for name in binaries:
 values=[r['elapsed_ns'] for r in rows if r['binary']==name and not r['warmup']]
 draws=sorted(statistics.median(rng.choices(values,k=len(values))) for _ in range(2000))
 summary.append(dict(binary=name,median_ns=statistics.median(values),median_95ci_ns=[draws[49],draws[1949]],samples=len(values)))
(a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
(a.out/'binaries.json').write_text(json.dumps({k:dict(path=str(v),sha256=hashlib.sha256(v.read_bytes()).hexdigest()) for k,v in binaries.items()},indent=2)+'\n')
print('PASS ordinary website output parity and local full-rewrite observations; no VM or portable release claim')
