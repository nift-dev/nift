#!/usr/bin/env python3
"""Matched local CP19 observations, including a real persistent PTY session.

Reports samples and bootstrap intervals, not portable acceptance or a waiver.
The CP18 shell comparator may execute legacy; counters record that limitation.
Run serially under the shared heavy lock, after builds finish.
"""
import argparse,hashlib,json,os,platform,pty,random,select,statistics,subprocess,termios,time
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('--base',required=True);p.add_argument('--nift',required=True)
p.add_argument('--switch',required=True);p.add_argument('--out',type=Path,required=True)
p.add_argument('--samples',type=int,default=21);p.add_argument('--units',type=int,default=30)
p.add_argument('--cpu',type=int,help='pin this local run and children to one permitted Linux CPU')
p.add_argument('--no-stats',action='store_true',help='supplementary timing only; companion stats-on run proves VM admission')
p.add_argument('--workload',action='append',choices=['startup','eval','script','pty'])
a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
if a.cpu is not None:
 assert a.cpu in os.sched_getaffinity(0),'CPU outside the permitted affinity set'
 os.sched_setaffinity(0,{a.cpu})
workloads=a.workload or ['startup','eval','script','pty']
binaries={name:Path(value).resolve() for name,value in [('base',a.base),('default',a.nift),('switch',a.switch)]}
records=[];rng=random.Random(191027)

def save():
 (a.out/'samples.json').write_text(json.dumps(records,indent=2)+'\n')

def environment(folder,mode):
 env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')}
 env.update(HOME=str(folder),NIFT_VM=mode,NO_COLOR='1',TERM='dumb')
 if not a.no_stats:env['NIFT_VM_STATS']=str(folder/'stats')
 return env

def stats(folder):
 file=folder/'stats'
 return [json.loads(s) for s in file.read_text().splitlines()] if file.exists() else []

def read_prompt(fd,after_command=False):
 data=b'';deadline=time.monotonic()+15
 while not (data.endswith(b'$ ') and (not after_command or b'\r\n' in data)):
  remaining=deadline-time.monotonic()
  if remaining<=0:raise TimeoutError(repr(data))
  if select.select([fd],[],[],remaining)[0]:
   chunk=os.read(fd,65536)
   if not chunk:raise RuntimeError('PTY closed before prompt: '+repr(data))
   data+=chunk
 return data.decode(errors='replace')

def run(name,mode,workload,sample):
 folder=a.out/f'{workload}-{name}-{mode}-{sample}';folder.mkdir()
 binary=binaries[name];env=environment(folder,mode)
 row=dict(binary=name,mode=mode,workload=workload,sample=sample,warmup=sample<0)
 if workload=='pty':
  master,slave=pty.openpty();attrs=termios.tcgetattr(slave);attrs[3]&=~termios.ECHO
  termios.tcsetattr(slave,termios.TCSANOW,attrs)
  proc=subprocess.Popen([str(binary),'-i'],stdin=slave,stdout=slave,stderr=slave,cwd=folder,env=env)
  os.close(slave);transcript='';latencies=[]
  try:
   transcript+=read_prompt(master);os.write(master,b'x:=0\n');transcript+=read_prompt(master,True)
   for i in range(1,a.units+1):
    before=time.perf_counter_ns();os.write(master,b'(x+=1)\n');reply=read_prompt(master,True)
    latencies.append(time.perf_counter_ns()-before);transcript+=reply
    # Prompt is checked separately from the exact scalar result, and terminal
    # line endings are normalized without stripping arbitrary output.
    lines=reply.split('\r\n')
    assert len(lines)==3 and lines[1]==str(i) and lines[0].endswith('(x+=1)'),(row,reply)
    assert 'error:' not in reply,(row,reply)
   os.write(master,b'exit\n');proc.wait(timeout=15)
   assert proc.returncode==0,(row,proc.returncode)
  finally:
   if proc.poll() is None:proc.kill();proc.wait()
   os.close(master);(folder/'transcript.txt').write_text(transcript)
  row.update(elapsed_ns=statistics.median(latencies),unit_latencies_ns=latencies,status=proc.returncode)
 else:
  if workload=='startup':argv=['-i'];source='exit\n';expected=''
  elif workload=='eval':argv=['eval','1+2*3'];source=None;expected='7\n'
  elif workload=='script':argv=['-c','i:=0;while(i<1000){i+=1};print(i)'];source=None;expected='1000\n'
  else:raise AssertionError(workload)
  before=time.perf_counter_ns()
  proc=subprocess.run([str(binary),*argv],input=source,cwd=folder,env=env,text=True,capture_output=True,timeout=30)
  elapsed=time.perf_counter_ns()-before
  (folder/'stdout.txt').write_text(proc.stdout);(folder/'stderr.txt').write_text(proc.stderr)
  assert (proc.returncode,proc.stdout,proc.stderr)==(0,expected,''),(row,proc)
  row.update(elapsed_ns=elapsed,status=proc.returncode)
 rows=stats(folder);row['stats']=rows
 if not a.no_stats and mode=='require' and (workload=='script' or (workload=='eval' and name!='base')):
  assert len(rows)==1 and rows[0]['outcome']=='vm' and rows[0]['vm_executions']==1 and rows[0]['legacy_executions']==0,row
 if not a.no_stats and workload=='pty' and name!='base' and mode=='require':
  assert len(rows)==a.units+1 and all(s['outcome']=='vm' and s['vm_executions']==1 and s['legacy_executions']==0 for s in rows),row
 records.append(row);save()

provenance=dict(local_only=True,system=platform.platform(),flags='-std=c++17 -O2 -Wall -Wextra -pedantic -pthread',
 binaries={k:dict(path=str(v),sha256=hashlib.sha256(v.read_bytes()).hexdigest()) for k,v in binaries.items()},
 cpu=subprocess.check_output(['lscpu','--json'],text=True),samples=a.samples,units=a.units,
 telemetry_enabled=not a.no_stats,workloads=workloads,cpu_affinity=sorted(os.sched_getaffinity(0)),pin_requested=a.cpu,
 limitations=['CP18 shell/eval use the existing legacy comparator; counters retained.',
 'Local wall-clock observations do not establish dedicated-hardware release gates.',
 'PTY and scripted workloads remain separate.'])
(a.out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
for workload in workloads:
 for sample in range(-2,a.samples):
  order=[(name,mode) for name in binaries for mode in ['0','require']];rng.shuffle(order)
  for name,mode in order:run(name,mode,workload,sample)
 print('Measured',workload,flush=True)

def interval(values):
 draws=[statistics.median(rng.choices(values,k=len(values))) for _ in range(2000)];draws.sort()
 return [draws[49],draws[1949]]
summary=[]
for workload in workloads:
 for name in binaries:
  for mode in ['0','require']:
   values=[r['elapsed_ns'] for r in records if r['workload']==workload and r['binary']==name and r['mode']==mode and not r['warmup']]
   summary.append(dict(workload=workload,binary=name,mode=mode,samples=len(values),median_ns=statistics.median(values),median_95ci_ns=interval(values)))
(a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
comparisons=[]
for workload in workloads:
 for mode in ['0','require']:
  base={r['sample']:r['elapsed_ns'] for r in records if r['binary']=='base' and r['workload']==workload and r['mode']==mode and not r['warmup']}
  for name in ['default','switch']:
   candidate={r['sample']:r['elapsed_ns'] for r in records if r['binary']==name and r['workload']==workload and r['mode']==mode and not r['warmup']}
   keys=sorted(base);draws=[]
   for _ in range(2000):
    selection=rng.choices(keys,k=len(keys))
    draws.append(statistics.median(candidate[k] for k in selection)/statistics.median(base[k] for k in selection))
   draws.sort();comparisons.append(dict(workload=workload,mode=mode,binary=name,ratio_of_medians=statistics.median(candidate.values())/statistics.median(base.values()),paired_95ci=[draws[49],draws[1949]]))
(a.out/'comparisons.json').write_text(json.dumps(comparisons,indent=2)+'\n')
print('PASS matched local samples; no portable performance gate claimed')
