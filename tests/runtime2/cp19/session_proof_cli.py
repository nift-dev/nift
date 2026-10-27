#!/usr/bin/env python3
"""Full-session higher-order and reserved-binding legacy-channel witnesses."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--legacy',required=True)
p.add_argument('--out',type=Path,required=True);a=p.parse_args();rows=[];a.out.parent.mkdir(parents=True,exist_ok=True)
cases=[
 ('discarded-open-result','a:=[1];fn(g(p)){p(0);return 0};warm:=g(v=>[1,2].flatten())\na.push("bad");print("FORBIDDEN")\ng(v=>[3,4].flatten())\nexit\n',0),
 ('unused-open-result','a:=[1];fn(g(p)){q:=p(0);return 0};warm:=g(v=>[1,2].flatten())\na.push("bad");print("FORBIDDEN")\ng(v=>[3,4].flatten())\nexit\n',0),
 ('original-shared-callable-replacement', 'fn(makeo()){a:=[()=>1];return {"putv":v=>{a[0]=v;return 0},"readv":()=>{q:=a[0];return q()*2}}};o:=makeo();warm:=o.putv(()=>1)\no.putv(()=>"bad");print("WROTE");o.readv()\nexit\n', 1),
 ('nested-call-return-prefix','fn(arg()){return 1};fn(readx(v)){return x*2};fn(h()){return readx(arg())};throw error("stop");x:=1\nx:="bad"\nh()\nexit\n',2),
 ('unwarmed-higher-order','a:=[1];fn(g(p)){return p(0)+a[0]}\na.push("bad")\ng(v=>v+2)\ng(v=>"text")\nexit\n',0),
 ('unwarmed-returned','a:=[1];fn(g(p)){q:=p();return q(0)+a[0]}\na.push("bad")\ng(()=>v=>v+2)\nexit\n',0),
 ('error-cell-arguments','m:="boom";c:="user.app.test"\ne:=error(m,c)\n(e.message)\n(e.code)\nexit\n',0),
 ('builtin-cell-arguments','s:="abc";q:="b"\ns.contains(q)\nexit\n',0),
 ('struct-cell-arguments','struct(Point){v:=0;fn(addv(a,b)){return a*10+b}};p:=Point();x:=1;y:=2\np.addv(x,y)\nexit\n',0),
 ('named-cell-arguments','fn(addv(a,b)){return a*10+b};x:=1;y:=2\naddv(x,y)\nexit\n',0),
 ('lambda-cell-arguments','f:=(a,b)=>(a*10+b);x:=1;y:=2\nf(x,y)\nexit\n',0),
 ('captured-callable-string','fn(makeo()){f:=()=>1;return {"putv":v=>{f=v;return 0},"readv":()=>f()}};o:=makeo();warm:=o.putv(()=>2)\no.putv("text");print("ONCE");o.readv()\no.putv(()=>3)\no.readv()\nexit\n',1),
 ('same-unit-string-function','f:="text"\nf = ()=>3;f()\nexit\n',0),
 ('function-to-string','f:=()=>1\nf = "text"\nf\nexit\n',0),
 ('string-to-function','f:="text"\nf = ()=>1\nf()\nexit\n',0),
 ('saved-function-to-string','f:=()=>1;old:=f\nf = "text"\nold()\nexit\n',0),
 ('reader-function-to-string','f:=()=>1;g:=()=>f()\nf = "text"\ng()\nexit\n',1),
 ('shared-callable-replacement','fn(applycb(v)){return v()};fn(makeo()){a:=[()=>1];return {"putv":v=>{a[0]=v;return 0},"readv":()=>{q:=a[0];return applycb(q)*2}}};o:=makeo();warm:=o.putv(()=>1)\no.putv(()=>"bad");print("WROTE");o.readv()\nexit\n',1),
 ('dormant-selector-call','a:=[1];fn(makec()){rows:=[()=>7];for(row:rows){return ()=>row}};c:=makec()\na.push("bad");print("ONCE")\na.stringify()\nq:=c()\nq()\nexit\n',0),
 ('exact','a:=[1];fn(g(p)){return p(0)+a[0]};warm:=g(v=>v+1)\na.push("bad");print("FORBIDDEN")\na.stringify()\ng(v=>v+1)\ng(v=>"text")\nexit\n',0),
 ('returned','a:=[1];fn(g(p)){q:=p();return q(0)+a[0]};warm:=g(()=>v=>v+1)\na.push("bad")\ng(()=>v=>v+1)\nexit\n',0),
 ('field','a:=[1];fn(g(p)){return p.cb(0)+a[0]};warm:=g({"cb":v=>v+1})\na.push("bad")\ng({"cb":v=>v+2})\nexit\n',0),
 ('element','a:=[1];fn(g(p)){q:=p[0];return q(0)+a[0]};warm:=g([v=>v+1])\na.push("bad")\ng([v=>v+2])\nexit\n',0),
 ('collection','a:=[1];fn(g(p)){r:=[1,2].map(p);return r.stringify()};warm:=g(v=>v+1)\na.push("bad")\ng(v=>v+2)\ng(v=>"x")\nexit\n',0),
 ('comparator','a:=[1];fn(g(p)){r:=[3,1];r.sort(p);return r.stringify()};warm:=g((l,r)=>l<r)\na.push("bad")\ng((l,r)=>r<l)\nexit\n',0),
 ('selector','a:=[1];fn(g(p)){r:=[{"k":2},{"k":1}];s:=r.sort_by(p);return s.stringify()};warm:=g(v=>v.k)\na.push("bad")\ng(v=>v.k)\nexit\n',0),
 ('mutual','a:=[1];fn(f(p,n)){if(n<=0){return p(1)+a[0]};return g(p,n-1)};fn(g(p,n)){if(n<=0){return p(1)+a[0]};return f(p,n-1)};warm:=f(v=>v+1,2)\na.push("bad")\ng(v=>"X",3)\nexit\n',0),
 ('current-effects','n:=0;a:=[1];fn(g(p)){return p(0)+a[0]};warm:=g(v=>v+1)\na.push("bad")\ng(v=>(n+=1)+v)\nn\nexit\n',0),
 ('arguments-once','n:=0;a:=[1];fn(g(p)){return p((n+=1))+a[0]};warm:=g(v=>v)\na.push("bad")\nn\ng(v=>v*2)\nn\nexit\n',0),
 ('current-error','n:=0;a:=[1];fn(g(p)){return p(0)+a[0]};warm:=g(v=>v+1);saved:=error("seed")\na.push("bad")\ntry{g(v=>{n+=1;throw error("boom")})}catch(e){saved=e}\nn\n(saved.message)\n(saved.code)\nexit\n',0),
 ('sibling-effects','fn(makeo()){a:=[1];return {"putv":v=>{a[0]=v;return 0},"readv":()=>a[0]*2}};o:=makeo();warm:=o.putv(0);a:=[1];fn(g(p)){return p(0)};warmg:=g(v=>1);saved:=error("seed")\na.push("bad")\ng(v=>o.putv("bad"))\ntry{o.readv()}catch(e){saved=e}\n(saved.message)\nexit\n',1),
 ('replacement','a:=[1];fn(g(p)){return p(0)*2};warm:=g(v=>v+1);old:=g\na.push("bad")\nfn(g(p)){return p(0)+"!"};after:=g(v=>v+2)\nafter\nold(v=>v+3)\nexit\n',0),
 ('constructor','struct(Point){v:=1};a:=[1];fn(g(p)){q:=Point();return p(q.v)};warm:=g(v=>v+1)\na.push("bad")\nstruct(Point){v:=2}\ng(v=>v+3)\nexit\n',0),
 ('reserved-scalar','fn(readx()){return x+1};throw error("stop");x:=1\nx:="s"\nreadx()\nexit\n',1),
 ('reserved-numeric-reader','fn(readx()){return x*2};throw error("stop");x:=1\nx:="s";print("ONCE")\nreadx()\nexit\n',2),
 ('reserved-nested-return','fn(readx()){return x*2};fn(h()){return readx()};throw error("stop");x:=1\nx:="s"\nh()\nexit\n',2),
 ('reserved-schema','struct(First){v:=1};struct(Second){v:=2};fn(readp()){return p.v};throw error("stop");p:=First()\np:=Second()\nreadp()\nexit\n',1),
 ('reserved-const','throw error("stop");a:=[1]\ntick:=0;const a:=[2]\na.stringify()\nexit\n',1),
 ('named-alias-place','fn(g(p)){return p+1};f:=g;a:=[1]\nf(a[0])\nexit\n',0),
 ('multiple-named','fn(g(p)){return p+1};fn(h(p)){return p+2};a:=[g,h]\nf:=a[0];f(3)\nf = a[1]\nf(3)\nexit\n',0),
]
for name,source,errors in cases:
 oracle=None
 for lane,binary,mode in [('legacy',a.legacy,'0'),('mode1',a.nift,'1'),('require',a.nift,'require')]:
  with tempfile.TemporaryDirectory(prefix='cp19-proof-cli-') as td:
   stats=Path(td)/'stats';env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')}
   env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
   r=subprocess.run([str(Path(binary).resolve()),'-i'],input=source,cwd=td,env=env,text=True,capture_output=True,timeout=30)
   counters=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
   row=dict(case=name,lane=lane,source=source,status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=counters)
   rows.append(row);a.out.write_text(json.dumps(rows,indent=2)+'\n')
   channels=(r.returncode,r.stdout,r.stderr)
   if oracle is None:
    oracle=channels;assert r.returncode==0 and sum(x.startswith('error: ') for x in r.stderr.splitlines())==errors,row
   else:
    assert channels==oracle,(oracle,row)
    assert len(counters)==len(source.splitlines())-1,row
    assert all(x['vm_executions']==1 and x['legacy_executions']==0 and x['outcome'] in ('vm','vm-error') for x in counters),row
    assert sum(x['outcome']=='vm-error' for x in counters)==errors,row
print('PASS full-session proof and remaining-admission witnesses:',len(rows),'legacy-channel observations')
