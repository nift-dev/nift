from pathlib import Path
import argparse,json,os,subprocess,tempfile
CASES=[
 ('scalar','1+2'),('nested','{"a":[[1,2],{"b":true}]}'),('collection','[1,2,2].unique()'),
 ('nested_selection','{"a":[1,2]}.get("a")[0]'),('fresh_error_origin','error("x").source'),
 ('error_array','[error("x")].stringify()'),('error_array_pretty','[error("x")].prettify()'),
 ('error_object_highlight','{"e":error("x")}.highlight()'),
 ('effect_error_present','[print("once"),error("x")].stringify()'),
 ('null_error_message','error("x").cause.message'),('null_error_code','error("x").cause.code'),
 ('null_error_source','error("x").cause.source'),('null_error_present','error("x").cause.stringify()'),
 ('synthetic_return_map','[1].map(1)'),('synthetic_return_filter','[1].filter(true)'),
 ('synthetic_return_find','[1].find(null)'),('synthetic_return_sort','[1].sort_by(null)'),
 ('multiline_collection','\n  [1].map(1)'),('multiline_runtime','\n  1/0'),
 ('parse_failure','1 +'),('trailing_tokens','1; touch("marker")'),
 ('whole_refusal','[touch("marker"),x => x]'),
 ('executed_failure','[print("once"),open("missing")]'),
 ('host_read','open("input")'),('shortcircuit','true || open("missing")'),
 ('ordinary_copy','copy({"a":[1,2]})'),('invalid_callback_string','[1].map("x")')]
ap=argparse.ArgumentParser();ap.add_argument('--nift',required=True);ap.add_argument('--out',required=True);args=ap.parse_args();rows=[]
for name,expression in CASES:
 for mode in ['0','1','require','classify','classify-all']:
  with tempfile.TemporaryDirectory(prefix='cp19-independent-') as tmp:
   root=Path(tmp);(root/'input').write_text('content');env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(NIFT_VM=mode,NIFT_VM_STATS=str(root/'stats'),HOME=tmp)
   p=subprocess.run([str(Path(args.nift).resolve()),'eval',expression],cwd=root,env=env,capture_output=True,text=True,timeout=30)
   st=[json.loads(x) for x in(root/'stats').read_text().splitlines()] if(root/'stats').exists() else []
   row=dict(name=name,expression=expression,mode=mode,exit=p.returncode,stdout=p.stdout,stderr=p.stderr,stats=st,marker=(root/'marker').exists());rows.append(row)
 for mode in ['1','require']:
  a=rows[-5];b=next(x for x in rows[-5:] if x['mode']==mode)
  if any(a[k]!=b[k] for k in ['exit','stdout','stderr']) and b['stats'] and b['stats'][0]['vm_executions']:
   print('DIFFERENCE',name,mode,'legacy:',a['exit'],repr(a['stdout']),repr(a['stderr']),'VM:',b['exit'],repr(b['stdout']),repr(b['stderr']))
Path(args.out).write_text(json.dumps(dict(binary=str(Path(args.nift).resolve()),cases=rows),indent=2))
print('Recorded',len(rows),'observations')
