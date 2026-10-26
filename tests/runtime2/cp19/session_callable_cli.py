#!/usr/bin/env python3
"""Persistent aggregate/captured callable specialization, with legacy channel parity."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
binary=str(Path(a.nift).resolve());a.out.parent.mkdir(parents=True,exist_ok=True);records=[]
cases=[
 ('lambda-named-alias','f:=()=>1;fn(g()){return 2};old:=f\nf = g\nf()\n(f==g)\nf = ()=>3\nf()\ng()\nold()\nexit\n',8),
 ('lambda-alias-rebind','f:=()=>1;g:=()=>2;old:=f;reader:=()=>f()\nf = g\nf()\nold()\nreader()\n(f==g)\nexit\n',6),
 ('lambda-alias-arity','f:=()=>1;g:=v=>v+2;old:=f\nf = g\nf(3)\nold()\nexit\n',4),
 ('replacement-loop-capture','fn(g()){return 1};fn(makec()){caparr:=[[1]];return v=>{for(row:caparr){row.push(v)};return caparr.stringify()}};c:=makec();warm:=c(0)\nfn(g()){return 3}\ng()\nc(2)\nexit\n',4),
 ('wrapper-lexical-shadow','a:=[[1]];f:=()=>a.stringify()\nfn(makec()){a:=[[1]];return v=>{for(row:a){row.push(v)};return a.stringify()}};c:=makec();c(0);saved:=f()\nf()\nc(2)\na.stringify()\nexit\n',5),
 ('nested-loop-mutation','a:=[[1],[2]]\nfor(v:a){v.push(2.5)}\na.stringify()\nexit\n',3),
 ('nested-loop-snapshot','a:=[[1],[2]];old:=a\nfor(v:a){v.push(2)}\na.stringify()\nold.stringify()\nexit\n',4),

 ('captured-lexical-shadow','n:="GLOBAL";fn(makec()){n:=10;return ()=>n+1};c:=makec()\nc()\nn\nexit\n',3),
 ('captured-array-numeric','fn(makec()){a:=[1];return v=>{a.push(v);return a.stringify()}};c:=makec();c(0)\nc(2.5)\nc(3)\nexit\n',3),
 ('const-captured-kind','fn(makec()){const a:=[1];return v=>a.push(v)};c:=makec()\nprint("ONCE");c("bad")\nexit\n',2),
 ('lambda-rebind-collection','f:=()=>1\nprint("ONCE");f = v=>v+1;[1,2].map(f).stringify()\nf(3)\nexit\n',3),
 ('lambda-rebind','f:=()=>1;oldf:=f;readf:=()=>f()\nf = ()=>2\nf()\noldf()\nreadf()\n(f==oldf)\nexit\n',6),
 ('lambda-rebind-param','f:=v=>v+1;oldf:=f\nf = v=>v*2\nf(3)\noldf(3)\nexit\n',4),
 ('lambda-rebind-capture','f:=()=>1;fn(setf()){n:=7;f = ()=>n}\nsetf()\nf()\nexit\n',3),
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
  root=Path(td);baseline=None;errors=1 if name=='const-captured-kind' else 0
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
   if name=='nested-loop-snapshot':
    # D-040 ordinary values copy on write; legacy's outer alias is deliberately different.
    assert baseline==(0,'[[1,2],[2,2]]\n[[1,2],[2,2]]\n',''),baseline
    assert channels==(0,'[[1,2],[2,2]]\n[[1],[2]]\n',''),record
   else:
    assert channels==baseline, (baseline,record)
   assert len(rows)==units and all(x['outcome'] in ('vm','vm-error') and x['vm_executions']==1 and x['legacy_executions']==0 for x in rows),record
   assert len([x for x in rows if x['outcome']=='vm-error'])==errors,record
print(f'PASS retained aggregate/captured callable flow: {len(records)} observations, affirmative VM execution')
