import os,re,json,tempfile,subprocess
from pathlib import Path
binary=str(Path(os.environ.get('NIFT_PREFIX_STATS','.build/cp410-implementation/traversal/stats-nift')).resolve())
with tempfile.TemporaryDirectory() as d:
 root=Path(d);files=root/'files';files.mkdir()
 def probe(pattern):
  q=subprocess.run([binary,'-'],input='print(ls('+json.dumps(pattern)+').size())',cwd=root,text=True,capture_output=True,env={**os.environ,'NIFT_TEST_GLOB_PREFIX_STATS':'1'},check=True)
  line=re.search(r'glob-prefix (.*)',q.stderr);assert line,q.stderr
  stats={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line[1])}
  return int(q.stdout),stats
 for n in (32,128,512):
  for i in range(n):(files/('%04d.dat'%i)).touch()
  count,s=probe('files/*.dat');assert count==n and s==dict(terminal_paths=n,prefix_nodes=1,prefix_resolves=1,full_display_paths=0,final_values=n),(n,s)
 count,s=probe('files/**/*.missing');assert count==0 and all(v==0 for v in s.values()),s
 directory=files.as_posix()
 if os.environ.get('MSYSTEM'):directory=subprocess.check_output(['cygpath','-m',directory],text=True,encoding='utf-8').strip()
 count,s=probe(directory+'/*.dat');assert count==512 and s==dict(terminal_paths=512,prefix_nodes=0,prefix_resolves=0,full_display_paths=0,final_values=512),s
 count,s=probe('files/**/**/**/*.dat');assert count==512 and s['final_values']==512 and s['prefix_resolves']==1 and s['full_display_paths']==0,s
 if os.name!='nt' and not os.environ.get('MSYSTEM'):
  (files/'alias.dat').symlink_to('0000.dat')
  count,s=probe('files/*.dat');assert count==513 and s['full_display_paths']==1 and s['prefix_resolves']==1 and s['final_values']==513,s
print('PASS shared-prefix scaling, empty/absolute bypass, repeated glob and leaf alias counters')
