#!/usr/bin/env python3
"""Real shell/-i/.niftrc state on the shared VM; refusal gets no coverage credit."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--nift',required=True);ap.add_argument('--out',type=Path,required=True)
a=ap.parse_args();binary=str(Path(a.nift).resolve());a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text('')
cases=[
 ('invocation_readonly_number',['-i'],'x:=5\nprint("ONCE");args.push(3)\nx\nargs.stringify()\nexit\n','',4),
 ('invocation_readonly',['-i'],'x:=5\nprint("ONCE");args.push("bad")\nx\nargs.stringify()\nexit\n','',4),
 ('invocation',['-i'],'cmd\nargs.stringify()\nexit\n','',2),
 ('invocation_inline',['-i','-c','x:=2','--','hello','-literal'],'cmd\nargs.stringify()\nx\nexit\n','',4),
 ('invocation_rc',['-i'],'saved\nexit\n','saved:=cmd',2),
 ('native_name',['-i'],'printf:=9\nprintf\ncommand /usr/bin/printf "%s\\n" "NATIVE"\nexit\n','',2),
 ('bindings',['-i'],'x:=1\nprint(x)\nx += 4\nprint(x)\nfn(getx()){return x}\ngetx()\nexit\n','',6),
 ('closure',['-i'],'fn(makec()){n:=10;return ()=>{n+=1;return n}};c:=makec()\nc()\nc()\nexit\n','',3),
 ('nested',['-i'],'fn(makec()){n:=10;return ()=>()=>{n+=1;return n}};outer:=makec()\ninner:=outer()\ninner()\ninner()\nexit\n','',4),
 ('skip_or',['-i'],'x:=0\ncommand /usr/bin/true || command /usr/bin/printf "%s\\n" "$[(x+=1)]"\nx\nexit\n','',2),
 ('skip_and',['-i'],'x:=0\ncommand /usr/bin/false && command /usr/bin/printf "%s\\n" "$[(x+=1)]"\nx\nexit\n','',2),
 ('interpolation',['-i'],'x:=7\ncommand /usr/bin/printf "%s\\n" "$[x]"\nx += 1\ncommand /usr/bin/printf "%s\\n" "$[x]"\nexit\n','',4),
 ('rc',['-i'],'x\nx += 2\ngetx()\nexit\n','x:=5;fn(getx()){return x}',4),
 ('rc_output',['-i'],'x\nexit\n','print("RC");x:=5',2),
 ('file',['-i','@script'],'x += 3\ngetx()\ncmd\nargs.stringify()\nexit\n','',5),
 ('stdin',['-i','-'],'x:=2;print(x)','',1),
 ('inline',['-i','-c','x:=2;fn(getx()){return x}'],'x += 3\ngetx()\nexit\n','',3),
 ('multiline',['-i'],'x:=1\nfn(getx()){\nreturn x\n}\ngetx()\nexit\n','',3),
 ('assignment_condition',['-i'],'x:=0;if(x=1){print(x)};print(x)\nx\nexit\n','',2),
 ('assignment_return',['-i'],'x:=0\nfn(f()){return 1+(x=3)}\nf()\nx\nexit\n','',4),
 ('assignment_index',['-i'],'a:=[1,2];i:=0\n(a[1]=(i+=1))\na.stringify()\ni\nexit\n','',4),
 ('assignment_expr',['-i'],'x:=1\n(x=2)\n(x+=3)\n1+(x=7)\nx\nexit\n','',5),
 ('assignment_nested',['-i'],'x:=1;y:=2\n(x=(y=3))\n(x+y)\n(false && (x=8))\nx\nexit\n','',5),
 ('increments',['-i'],'x:=1\n(x++)\n(++x)\nprint(x)\nexit\n','',4),
 ('signature',['-i'],'fn(addv(v)){return v+1};addv(0)\naddv(8)\nexit\n','',2),
 ('capture_param',['-i'],'fn(makec(n)){return ()=>{n+=1;return n}};c:=makec(10)\nc()\nc()\nexit\n','',3),
 ('first_lambda',['-i'],'f:=v=>v+1\nf(8)\nf("x")\nexit\n','',3),
 ('captured_lambda',['-i'],'fn(makec()){n:=10;return v=>{n+=v;return n}};c:=makec()\nc(2)\nc(3)\nexit\n','',3),
 ('first_call',['-i'],'fn(addv(v)){return v+1}\naddv(8)\naddv("x")\nexit\n','',3),
 ('redefinition',['-i'],'fn(g0()){return 1};f:=g0;getg:=()=>g0()\nf()\nfn(g0()){return 2}\nf()\ngetg()\n(f==g0)\nexit\n','',6),
 ('alias_function',['-i'],'fn(addv(v)){return v+1}\nf:=addv\nf(8)\nexit\n','',3),
 ('callback_function',['-i'],'fn(inc(v)){return v+1}\n[1,2].map(inc).stringify()\nexit\n','',2),
 ('callback_mutable',['-i'],'fn(inc(v)){v+=1;return v}\n[1,2].map(inc).stringify()\nexit\n','',2),
 ('quoted_callback',['-i'],'a:=[1,2]\na.map("v=>v+1").stringify()\na.map("v=>v+1").filter("v=>v>2").stringify()\nexit\n','',3),
 ('array_callback',['-i'],'a:=[1,2]\nfn(inc(v)){return v+1}\na.map(inc).stringify()\nexit\n','',3),
 ('array_sort',['-i'],'a:=[3,1,2]\nfn(cmpv(a0,b0)){return a0<b0}\na.sort(cmpv).stringify()\nexit\n','',3),
 ('return_string',['-i','-c','x:=2;return "hello"'],'x\nexit\n','',2),
 ('return_null',['-i','-c','return null'],'exit\n','',1),
 ('tail_string',['-i'],'fn(greet()){return "hi"};greet()\ngreet()\nexit\n','',2),
 ('cow',['-i'],'a:=[1,2]\nb:=a\nb.push(3)\na.stringify()\nb.stringify()\nexit\n','',5),
]
count=0
for name,argv,stdin,rc,units in cases:
 with tempfile.TemporaryDirectory(prefix='cp19-session-cli-') as td:
  root=Path(td)
  script=root/'initial.nift';script.write_text('x:=2;fn(getx()){return x}')
  actual_argv=[str(script) if x=='@script' else x for x in argv]
  if rc:(root/'.niftrc').write_text(rc)
  baseline=None
  for mode in ['0','1','require']:
   stats=root/'stats';stats.unlink(missing_ok=True)
   env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
   p=subprocess.run([binary,*actual_argv],input=stdin,text=True,cwd=td,env=env,capture_output=True,timeout=30)
   rows=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
   r=dict(case=name,mode=mode,status=p.returncode,stdout=p.stdout,stderr=p.stderr,stats=rows)
   with a.out.open('a') as f:f.write(json.dumps(r)+'\n')
   channels=(p.returncode,p.stdout,p.stderr);count+=1
   if mode=='0':baseline=channels;continue
   if name == 'cow':
    # Accepted D-040 array value-copy semantics; retain the raw legacy alias
    # result as evidence instead of pretending these channels are identical.
    assert baseline == (0,'3\n[1,2,3]\n[1,2,3]\n',''),baseline
    assert channels == (0,'3\n[1,2]\n[1,2,3]\n',''),r
   else: assert channels==baseline,(name,baseline,r)
   assert len(rows)==units,(name,units,r)
   assert len([s for s in rows if s['outcome']=='vm-error']) == (1 if name in ('invocation_readonly','invocation_readonly_number') else 0),r
   assert all(s['vm_executions']==1 and s['legacy_executions']==0 and s['outcome'] in ('vm','vm-error') for s in rows),r
print(f'PASS real session entry checks (parity and explicit D-040 witness): {count} observations')
