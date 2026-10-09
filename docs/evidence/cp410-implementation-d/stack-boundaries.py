from pathlib import Path
import subprocess,tempfile,json,os
root=Path(__file__).resolve().parent
bins={'normal':root/'nift','lifetime':root/'certification/.build/nift-sanitize-lifetime'}
rows=[]
with tempfile.TemporaryDirectory(prefix='nift-stack-boundary-') as d:
 p=Path(d);(p/'data').write_text('data')
 for kind in ['named','builtin','method']:
  for n in [20,40,48,50,60,80,96,110]:
   expr={'named':'f.exists()','builtin':'"x"','method':'"x"'}[kind]
   if kind=='method':expr+=' .trim()'*n
   else:expr=('id(' if kind=='named' else 'html_escape(')*n+expr+')'*n
   source='f:=file("data");fn(id(x)){return x};print('+expr+')\n';(p/'case.f').write_text(source)
   values={}
   for key,b in bins.items():
    q=subprocess.run([str(b),str(p/'case.f')],cwd=p,capture_output=True,text=True,timeout=60)
    values[key]={'exit':q.returncode,'stdout':q.stdout,'stderr':q.stderr.replace(str(p),'<ROOT>')}
   match=values['normal']==values['lifetime'];rows.append({'kind':kind,'n':n,'match':match,'values':values});print(kind,n,'PASS' if match else 'FAIL',flush=True)
 (root/'stack-boundaries.json').write_text(json.dumps(rows,indent=2)+'\n')
assert all(r['match'] for r in rows)
