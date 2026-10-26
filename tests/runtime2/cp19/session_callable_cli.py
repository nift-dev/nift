#!/usr/bin/env python3
"""Persistent aggregate/captured callable specialization, with legacy channel parity."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
binary=str(Path(a.nift).resolve());a.out.parent.mkdir(parents=True,exist_ok=True);records=[]
cases=[
 ('textually-double-binding','x:=1e0;fn(readv()){return x}\n(x=2.5)\nreadv()\nexit\n',3),
 ('numeric-global-contract','x:=1.5;fn(readv()){return x}\n(x=3)\nreadv()\n(x=5.5)\nreadv()\nexit\n',5),
 ('numeric-array-contract','a:=[1,2]\na.push(2.5)\na.stringify()\nexit\n',3),
 ('unrelated-parameter-shadow','x:=1\nfn(inc(x)){return x+1}\ninc(4)\nx\nexit\n',4),
 ('unrelated-local-shadow','x:=1\nfn(localv()){x:=7;return x}\nlocalv()\nx\nexit\n',4),
 ('quoted-named-callback','fn(inc(v)){return v+1};a:=[1,2]\na.map("inc").stringify()\na.map("v=>inc(v)").stringify()\nexit\n',3),
 ('captured-reader-redefinition','fn(g0()){return 1};fn(factory()){localf:=()=>g0();return ()=>localf()};c:=factory()\nfn(g0()){return 2}\nc()\nexit\n',3),
 ('object','o:={"f":v=>v+1}\n(o.f(2))\n(o.f(4))\nexit\n',3),
 ('array','a:=[v=>v*2]\nf:=a[0];f(3)\nf(5)\nexit\n',3),
 ('captured-function','fn(makecb()){g:=x=>x+1;return x=>g(x)};c:=makecb()\nc(2)\nc(4)\nexit\n',3),
 ('captured-object','fn(makecb()){o:={"f":x=>x+1};return x=>o.f(x)};c:=makecb()\nc(2)\nc(4)\nexit\n',3),
 ('object-reader-redefinition','fn(g(v)){return v+1};o:={"f":v=>g(v)}\nfn(g(v)){return v+2}\n(o.f(2))\nexit\n',3),
 ('object-alias-redefinition','fn(g(v)){return v+1};o:={"f":g}\nfn(g(v)){return v+2}\n(o.f(2))\nexit\n',3),
]
for name,source,units in cases:
 with tempfile.TemporaryDirectory(prefix='cp19-callable-') as td:
  root=Path(td);baseline=None;errors=0
  for mode in ['0','1','require']:
   stats=root/'stats';stats.unlink(missing_ok=True)
   env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
   r=subprocess.run([binary,'-i'],input=source,cwd=td,env=env,text=True,capture_output=True,timeout=30)
   rows=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
   record=dict(case=name,mode=mode,status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=rows);records.append(record)
   a.out.write_text(json.dumps(records,indent=2)+'\n')
   channels=(r.returncode,r.stdout,r.stderr)
   if baseline is None:
    baseline=channels;assert r.returncode==0 and bool(r.stderr)==bool(errors),record;continue
   assert channels==baseline, (baseline,record)
   assert len(rows)==units and all(x['outcome'] in ('vm','vm-error') and x['vm_executions']==1 and x['legacy_executions']==0 for x in rows),record
   assert len([x for x in rows if x['outcome']=='vm-error'])==errors,record
print(f'PASS retained aggregate/captured callable flow: {len(records)} observations, affirmative VM execution')
