#!/usr/bin/env python3
"""A relative glob resolves its common base once, not once per result."""
import json,os,re,subprocess,tempfile
from pathlib import Path
binary=str(Path(os.environ.get('NIFT','./nift')).resolve())
with tempfile.TemporaryDirectory() as d:
 root=Path(d);(root/'files').mkdir()
 for n in (32,128,512):
  for i in range(n):(root/'files'/f'entry-{i:06}.dat').touch()
  absolute=(root/'files'/'*.dat').as_posix()
  if os.environ.get('MSYSTEM'):absolute=subprocess.check_output(['cygpath','-m',absolute],text=True,encoding='utf-8').strip()
  for pattern,count,bases,matches in [('files/**/*.dat',n,1,n),('files/**/*.missing',0,0,0),(absolute,n,0,0)]:
   code='print(ls('+json.dumps(pattern,ensure_ascii=False)+').size())'
   p=subprocess.run([binary,'-e',code],cwd=root,text=True,encoding='utf-8',capture_output=True,env={**os.environ,'NIFT_TEST_GLOB_PATH_STATS':'1'})
   assert p.returncode==0 and p.stdout==str(count)+'\n',(p.stdout,p.stderr)
   m=re.search(r'glob-relative bases=(\d+) matches=(\d+)',p.stderr)
   assert m and tuple(map(int,m.groups()))==(bases,matches),p.stderr
 print('PASS relative-base scaling: 32/128/512; absolute and empty globs bypass canonicalization')
