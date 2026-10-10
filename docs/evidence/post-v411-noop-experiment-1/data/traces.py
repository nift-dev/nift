import pathlib,subprocess,os,json
p=pathlib.Path('/tmp/nift-status-experiment');f=pathlib.Path('/tmp/nift-noop-investigation');os.sched_setaffinity(0,{0,1,2,3})
for n in [100,1000,5000,10000]:
 for label,exe in [('baseline',str(p/'nift-baseline')),('candidate','/home/nick/Repositories/nift/nift/nift')]:
  subprocess.run(['strace','-f','-c','-o',str(p/f'trace-{n}-{label}.txt'),exe,'build'],cwd=f/f'fixture-{n}-nift',check=True,stdout=subprocess.DEVNULL);print(n,label,flush=True)
