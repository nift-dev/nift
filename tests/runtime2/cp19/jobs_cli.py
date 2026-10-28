"""Shell jobs retain CP21 ownership beside compiled persistent script commands."""
import argparse,json,os,subprocess,tempfile,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();rows=[]
for mode in ['0','1','require']:
 for end in ['exit\n','']:
  with tempfile.TemporaryDirectory(prefix='cp19-jobs-') as td:
   root=Path(td);(root/'.niftrc').write_text('seed:=40')
   env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(root/'stats'),NO_COLOR='1')
   commands='/bin/sh -c "echo $$ > child.pid; sleep 60" &\n/bin/sleep 0.05\nseed += 2\nseed\njobs\n'+end
   start=time.monotonic();r=subprocess.run([a.nift,'-i'],cwd=root,input=commands,env=env,text=True,capture_output=True,timeout=15)
   elapsed=time.monotonic()-start
   counters=[json.loads(line) for line in (root/'stats').read_text().splitlines()] if (root/'stats').exists() else []
   pid=int((root/'child.pid').read_text()) if (root/'child.pid').exists() else 0
   alive=False
   if pid:
    try:os.kill(pid,0);alive=True
    except ProcessLookupError:pass
   row=dict(mode=mode,end='exit' if end else 'eof',status=r.returncode,stdout=r.stdout,stderr=r.stderr,seconds=elapsed,pid=pid,alive=alive,stats=counters);rows.append(row)
   assert r.returncode==0 and '42' in r.stdout and pid>0 and not alive and elapsed<12,row
   if mode!='0':
    assert counters and sum(x['vm_executions'] for x in counters)>=3 and all(x['legacy_executions']==0 for x in counters),row
# Explicit proof that run() is refused before effects in VM-owned sessions.
with tempfile.TemporaryDirectory(prefix='cp19-run-') as td:
 env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM='require',NO_COLOR='1')
 r=subprocess.run([a.nift,'-i'],input='run("touch forbidden")\nx:=7\nx\nexit\n',cwd=td,env=env,text=True,capture_output=True,timeout=15)
 assert not (Path(td)/'forbidden').exists() and '7' in r.stdout and 'callable:run' in r.stderr,(r.stdout,r.stderr)
 rows.append(dict(case='run-refusal',stdout=r.stdout,stderr=r.stderr,status=r.returncode))
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(rows,indent=2)+'\n');print('PASS CP19 session jobs CLI: EOF/exit, rc persistence, compiled commands, no run fallback')
