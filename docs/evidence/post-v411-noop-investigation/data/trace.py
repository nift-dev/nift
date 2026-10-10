import pathlib,subprocess,json,os
p=pathlib.Path('/tmp/nift-noop-investigation');os.sched_setaffinity(0,set(json.loads((p/'identity.json').read_text())['cpus']))
for size in [100,1000,5000,10000]:
 for system in ['nift','ninja']:
  d=p/f'fixture-{size}-{system}';cmd=['/home/nick/Repositories/nift/nift/nift','build'] if system=='nift' else ['/tmp/build-systems-tools/ninja/usr/bin/ninja','-j4'];env=os.environ.copy();env['BUILD_TRACE']=str(d/'diagnostic.trace')
  subprocess.run(['strace','-f','-c','-o',str(p/f'strace-{size}-{system}.txt'),*cmd],cwd=d,env=env,check=True,stdout=subprocess.DEVNULL)
  print('strace',size,system,flush=True)
subprocess.run(['strace','-f','-e','trace=newfstatat,openat,readlink,getdents64','-o',str(p/'paths-100-nift.txt'),'/home/nick/Repositories/nift/nift/nift','build'],cwd=p/'fixture-100-nift',check=True,stdout=subprocess.DEVNULL)
subprocess.run(['perf','record','-e','cpu-clock','-g','-o',str(p/'perf.data'),'--','/home/nick/Repositories/nift/nift/nift','build'],cwd=p/'fixture-10000-nift',stdout=(p/'perf.stdout').open('w'),stderr=(p/'perf.stderr').open('w'))
