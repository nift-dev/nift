import argparse,json,os,subprocess,tempfile
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--nift',required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
binary=str(Path(a.nift).resolve());a.out.parent.mkdir(parents=True,exist_ok=True);records=[]
cases=[
 ('literal-type-refusal','x:=5\nprint("FORBIDDEN");1*"bad"\nx\nexit\n','5\n','type:*'),
 ('loop-nested-compilation-atomicity','a:=[[1]]\nprint("FORBIDDEN");for(v:a){v.push("bad")};bytes([65])\na.stringify()\nexit\n','[[1]]\n','builtin-bytes:bytes'),
 ('capture-nested-compilation-atomicity','fn(makec()){a:=[[1]];return v=>{for(row:a){row.push(v)};return a.stringify()}};c:=makec()\nprint("FORBIDDEN");c("bad");bytes([65])\nc(2)\nexit\n','"[[1,2]]"\n','builtin-bytes:bytes'),
 ('captured-struct','x:=5\nstruct(Point){x:=1};print("FORBIDDEN");fn(makep()){p:=Point();return ()=>p.x};f:=makep()\nx\nexit\n','5\n','session:captured-struct'),
 ('lambda-rebind-spelling','f:=v=>v+1\nprint("FORBIDDEN");f=v=>v*2\nf(3)\nexit\n','4\n','session:lambda-assignment-spelling'),
 ('struct-outer-shadow','v:=99\nstruct(Counter){v:=0;fn(get()){return v}};print("FORBIDDEN");c:=Counter()\nv\nexit\n','99\n','session:struct-dynamic-shadow'),
 ('struct-later-shadow','struct(Counter){v:=0;fn(get()){return v}};c:=Counter()\nprint("FORBIDDEN");v:=99\nc.get()\nexit\n','0\n','session:struct-dynamic-shadow'),
 ('struct-caller-shadow','struct(Counter){v:=0;fn(get()){return v}};c:=Counter()\nprint("FORBIDDEN");fn(caller(v)){return c.get()}\nc.get()\nexit\n','0\n','session:struct-dynamic-shadow'),
 ('local-dynamic-shadow','x:=1;fn(readx()){return x}\nprint("FORBIDDEN");fn(caller()){x:=7;return readx()}\nx\nreadx()\nexit\n','1\n1\n','session:dynamic-shadow'),
 ('parameter-dynamic-shadow','x:=1;fn(readx()){return x}\nprint("FORBIDDEN");fn(caller(x)){return readx()}\nx\nreadx()\nexit\n','1\n1\n','session:dynamic-shadow'),
 ('dynamic-flow','o:={"set":v=>v+1};x:=5\nprint("FORBIDDEN");o.set(4)\nx\nexit\n','5\n','session:legacy-method-precedence'),
 ('transaction','x:=5;f:=()=>x\nleaked:=77;print("FORBIDDEN");bytes([65])\nf()\nx\nexit\n','5\n5\n','builtin-bytes:bytes'),
 ('receiver','a:=[3,1,2]\nfn(cmpv(a0,b0)){a.push(9);return a0<b0}\na.sort(cmpv)\na.stringify()\nexit\n','[3,1,2]\n','method:sort'),
 ('new-function-before-declaration','fn(g0()){return 1};o:={"invoke":()=>g0()}\nprint("FORBIDDEN");newg();fn(newg()){return 2}\no.invoke()\nexit\n','1\n','fn-called-before-declaration'),
 ('prepared-loop','x:=0\nprint("FORBIDDEN");while((x+=1)<3){print(x)}\nx\nexit\n','0\n','prepared-unpreparable'),
 ('first-refusal','leaked:=77;print("FORBIDDEN");bytes([65])\nx:=5\nx\nexit\n','5\n','builtin-bytes:bytes'),
]
for name,source,expected,reason in cases:
 for mode in ['1','require']:
  with tempfile.TemporaryDirectory(prefix='cp19-refusal-') as td:
   stats=Path(td)/'stats'
   env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
   p=subprocess.run([binary,'-i'],input=source,cwd=td,env=env,text=True,capture_output=True,timeout=30)
   rows=[json.loads(x) for x in stats.read_text().splitlines()]
   row=dict(case=name,mode=mode,status=p.returncode,stdout=p.stdout,stderr=p.stderr,stats=rows);records.append(row)
   a.out.write_text(json.dumps(records,indent=2)+'\n')
   assert p.returncode==0 and p.stdout==expected,row
   assert p.stderr==f'error: NIFT_VM session refused: {reason}\n',row
   refused=[s for s in rows if s['outcome']=='require-error'];assert len(refused)==1,row
   assert refused[0]['reason']==reason and refused[0]['vm_executions']==refused[0]['legacy_executions']==0,row
   assert all(s['legacy_executions']==0 for s in rows),row
   assert len([s for s in rows if s['vm_executions']==1])==len(rows)-1,row
print(f'PASS session refusal/state preservation: {len(records)} observations, never counted as VM execution')
