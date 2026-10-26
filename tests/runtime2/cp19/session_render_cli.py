#!/usr/bin/env python3
"""Legacy compound-statement presentation, retained effects and rc output policy."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
binary=str(Path(a.nift).resolve());a.out.parent.mkdir(parents=True,exist_ok=True);records=[]
cases=[
 ('calls','fn(f()){return "hi"};f();f()\nexit\n','',1,0),
 ('prints','print("X");print("Y")\nexit\n','',1,0),
 ('push','a:=[1];a.push(2)\nexit\n','',1,0),
 ('map','a:=[1];a.map(v=>v+1)\nexit\n','',1,0),
 ('reduce','a:=[1,2];a.reduce((s,v)=>s+v,0)\nexit\n','',1,0),
 ('literals','"hello";3\nexit\n','',1,0),
 ('void-call','fn(f()){print("BODY")};f()\nexit\n','',1,0),
 ('return','x:=1;return (x=3)\nx\nexit\n','',2,0),
 ('aggregate','a:=[1,2];a\na.stringify()\nexit\n','',2,1),
 ('discard-prefix','s:="PRE";s;print("ONCE");a:=[1];a\na.stringify()\ns\nexit\n','',3,1),
 ('loop','x:=0;while(x<2){x;print(x);x+=1}\nexit\n','',1,0),
 ('increment','x:=1;x++\n(++x)\nx\nexit\n','',3,0),
 ('error-value','e:=error("E");e\n(e.message)\nexit\n','',2,1),
 ('rc-discard','x\nexit\n','print("RC");x:=1;x',2,0),
 ('rc-return','cmd\nexit\n','return 5',2,0),
 ('rc-inspector','cmd\nexit\n','[1,2]',2,0),
 ('rc-error','command /usr/bin/touch marker\nexit\n','a:=[1];a',1,1),
]
for name,source,rc,units,errors in cases:
 with tempfile.TemporaryDirectory(prefix='cp19-render-') as td:
  root=Path(td)
  if rc:(root/'.niftrc').write_text(rc)
  baseline=None
  for mode in ['0','1','require']:
   stats=root/'stats';stats.unlink(missing_ok=True)
   env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
   r=subprocess.run([binary,'-i'],input=source,cwd=td,env=env,text=True,capture_output=True,timeout=30)
   rows=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
   record=dict(case=name,mode=mode,status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=rows,marker_exists=(root/'marker').exists());records.append(record)
   a.out.write_text(json.dumps(records,indent=2)+'\n')
   channels=(r.returncode,r.stdout,r.stderr)
   if baseline is None:baseline=channels;continue
   assert channels==baseline and not record['marker_exists'],(baseline,record)
   assert len(rows)==units and all(x['vm_executions']==1 and x['legacy_executions']==0 for x in rows),record
   assert len([x for x in rows if x['outcome']=='vm-error'])==errors,record
   assert all(x['outcome'] in ('vm','vm-error') for x in rows),record
print(f'PASS compound REPL/rc presentation: {len(records)} observations, runtime failures distinguished from successes')
