#!/usr/bin/env python3
"""Persistent CP12b struct subset through actual shell/session routes."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
binary=str(Path(a.nift).resolve());a.out.parent.mkdir(parents=True,exist_ok=True);records=[]
schema='struct(Counter){v:=0;fn(add(n)){v+=n} fn(get()){return v}};c:=Counter()'
cases=[
 ('rc-schema','c.add(3)\nc.get()\nexit\n',3,0,['-i'],schema),
 ('file-schema','c.add(3)\nc.get()\nexit\n',3,0,['-i','@script']),
 ('inline-schema','c.add(3)\nc.get()\nexit\n',3,0,['-i','-c',schema]),
 ('stdin-schema',schema+';c.add(3);print(c.get())',1,0,['-i','-']),
 ('constructor-field-param','struct(Point){x:=0;fn(Point(x)){this.x=x} fn(get()){return x}}\np:=Point(7)\np.get()\nexit\n',3,0),
 ('unrelated-callback-shadow','struct(Point){x:=0;fn(get()){return x}};p:=Point()\n[1,2].map(x=>x+1).stringify()\np.get()\nexit\n',3,0),
 ('separate-schema','struct(Counter){v:=0;fn(add(n)){v+=n} fn(get()){return v}}\nc:=Counter()\nc.add(3)\nc.get()\nc.add(4)\nc.get()\nexit\n',6,0),
 ('field-read-write','struct(Point){x:=1;y:=2};p:=Point()\n(p.x)\np.x = 3\n(p.x)\n(p.y)\nexit\n',5,0),
 ('constructor','struct(Point){v:=0;fn(Point(n)){v=n} fn(get()){return v}}\np:=Point(7)\np.get()\nexit\n',3,0),
 ('method-nesting','struct(Counter){v:=0;fn(add(n)){v+=n} fn(twice(n)){add(n);add(n)} fn(get()){return v}};c:=Counter()\nc.twice(3)\nc.get()\nexit\n',3,0),
 ('retained-reader','struct(Point){x:=1};p:=Point();fn(readp()){return p.x}\np.x = 7\nreadp()\nexit\n',3,0),
 ('field-error','struct(Point){x:=1};p:=Point()\nprint("ONCE");p.x="bad"\n(p.x)\nexit\n',3,1),
 ('method-error-origin','struct(Point){x:=1;fn(fail()){throw error("origin")}};p:=Point();saved:=error("seed")\ntry{p.fail()}catch(e){saved=e}\n(saved.source)\n(saved.message)\nexit\n',4,0),
 ('multiple-schemas','struct(Left){v:=1};struct(Right){v:=2};l:=Left();r:=Right()\n(l.v)\n(r.v)\nl.v = 3\n(l.v)\n(r.v)\nexit\n',6,0),
 ('double-field','struct(Point){x:=1e0};p:=Point()\np.x = 2.5\n(p.x)\np.x = 3\n(p.x)\nexit\n',5,0),
]
for case in cases:
 name,source,units,errors=case[:4];argv=case[4] if len(case)>4 else ['-i'];rc=case[5] if len(case)>5 else '' 
 with tempfile.TemporaryDirectory(prefix='cp19-struct-') as td:
  root=Path(td);baseline=None
  script=root/'initial.nift';script.write_text(schema)
  actual_argv=[str(script) if x=='@script' else x for x in argv]
  if rc:(root/'.niftrc').write_text(rc)
  for mode in ['0','1','require']:
   stats=root/'stats';stats.unlink(missing_ok=True)
   env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
   r=subprocess.run([binary,*actual_argv],input=source,cwd=td,env=env,text=True,capture_output=True,timeout=30)
   rows=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
   record=dict(case=name,mode=mode,status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=rows);records.append(record)
   a.out.write_text(json.dumps(records,indent=2)+'\n')
   channels=(r.returncode,r.stdout,r.stderr)
   if baseline is None:
    baseline=channels;assert r.returncode==0 and bool(r.stderr)==bool(errors),record;continue
   assert channels==baseline,(baseline,record)
   assert len(rows)==units and all(x['outcome'] in ('vm','vm-error') and x['vm_executions']==1 and x['legacy_executions']==0 for x in rows),record
   assert len([x for x in rows if x['outcome']=='vm-error'])==errors,record
print(f'PASS persistent struct subset: {len(records)} observations, runtime failures separately counted')
