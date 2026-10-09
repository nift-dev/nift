#!/usr/bin/env python3
"""Exact glob presentation, hidden names, recursion and alias ordering."""
import json,os,subprocess,tempfile
from pathlib import Path
binary=str(Path(os.environ.get('NIFT','./nift')).resolve());baseline=os.environ.get('NIFT_BASELINE')
with tempfile.TemporaryDirectory() as d:
 root=Path(d)
 native_windows=os.name=='nt' or bool(os.environ.get('MSYSTEM'))
 literal='literal＊.dat' if native_windows else 'literal*.dat'
 for name in ['files/a.dat','files/.hidden.dat','files/.private/x.dat','files/sub/z.dat','files/sub/é.dat','files/sub/space name.dat','files/sub/deep/b.dat','files/'+literal,'outside/t.dat']:
  p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'x')
 links=os.name!='nt' and not os.environ.get('MSYSTEM')
 if links:
  for name,target in [('files/link.dat','sub/z.dat'),('files/dangling.dat','absent'),('files/linked-dir','sub'),('files/external.dat','../outside/t.dat')]:
   (root/name).symlink_to(target,target_is_directory=name.endswith('dir'))
 all_matches=['files/a.dat']+(['outside/t.dat','files/sub/z.dat'] if links else [])+['files/'+literal,'files/sub/deep/b.dat','files/sub/space name.dat','files/sub/z.dat','files/sub/é.dat']
 cases=[('files/**/*.dat',all_matches),('files/**/**/*.dat',all_matches),('files/**/**/**/*.dat',all_matches),('files/.private/*.dat',['files/.private/x.dat']),('files/**/.*',['files/.hidden.dat','files/.private']),('files/**/*.missing',[]),('files/sub/?.dat',['files/sub/z.dat']),('files/**/deep/*.dat',['files/sub/deep/b.dat'])]
 cases.append(('files/literal＊.?at' if native_windows else r'files/literal\*.?at',['files/'+literal]))
 if links:cases += [('files/linked-dir/*.dat',['files/sub/space name.dat','files/sub/z.dat','files/sub/é.dat']),('files/external*.dat',['outside/t.dat'])]
 absolute=(root/'files'/'*.dat').as_posix()
 if os.environ.get('MSYSTEM'):absolute=subprocess.check_output(['cygpath','-m',absolute],text=True,encoding='utf-8').strip()
 prefix=absolute[:-5];names=['a.dat',literal]+(['external.dat','link.dat'] if links else []);cases.append((absolute,sorted(prefix+name for name in names)))
 for pattern,expected in cases:
  code='print(ls('+json.dumps(pattern,ensure_ascii=False)+').stringify())'
  p=subprocess.run([binary,'-'],input=code,cwd=root,text=True,encoding='utf-8',capture_output=True)
  assert p.returncode==0,p.stderr
  assert json.loads(p.stdout)==expected,(pattern,p.stdout,expected)
  if baseline:
   q=subprocess.run([str(Path(baseline).resolve()),'-'],input=code,cwd=root,text=True,encoding='utf-8',capture_output=True)
   assert (p.returncode,p.stdout,p.stderr)==(q.returncode,q.stdout,q.stderr),(p,q)
 print('PASS',len(cases),'exact glob path/ordering/hidden/Unicode/alias contracts')
