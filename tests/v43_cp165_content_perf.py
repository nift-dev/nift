#!/usr/bin/env python3
import json, os, pathlib, subprocess, tempfile, time
BIN=pathlib.Path(os.environ.get('NIFT_BIN','./nift')).resolve()
for n in (100,1000,10000):
  with tempfile.TemporaryDirectory() as td:
    r=pathlib.Path(td); (r/'.nift').mkdir(); (r/'content').mkdir(); (r/'model').mkdir(); (r/'templates').mkdir(); (r/'public').mkdir()
    (r/'.nift/config.json').write_text(json.dumps({'config':{'content-dir':'content/','content-ext':'.md','output-dir':'public/','output-ext':'.html','default-template':'templates/template.html','incremental-mode':'modified','schemas':['model/site.schema'],'taxonomies':['model/site.tax']}}))
    (r/'model/site.schema').write_text('@schema(post) {\ntitle: string\ntags: taxonomy(tag)[]\n}\n')
    (r/'model/site.tax').write_text('@taxonomy(tag)\n')
    (r/'templates/template.html').write_text('@content\n')
    tracked=[]
    for i in range(n):
      name=f'p{i}'; tracked.append({'name':name,'title':name,'type':'post'}); (r/'content'/f'{name}.md').write_text(f'---\ntitle: Post {i}\ntags:\n  - tag-{i%100}\n---\nBody\n')
    (r/'.nift/tracked.json').write_text(json.dumps({'tracked':tracked}))
    t=time.perf_counter(); p=subprocess.run([str(BIN),'eval','--json','(project.content.post).size()'],cwd=r,text=True,capture_output=True,timeout=60); elapsed=time.perf_counter()-t
    if p.returncode or json.loads(p.stdout)!=n: raise SystemExit(f'{n}: failed: {p.stderr}')
    print(f'{n}: {elapsed:.3f}s')
