#!/usr/bin/env python3
"""Exact portable glob ordering and traversal contracts; optional A/B oracle."""
import os,json,subprocess,tempfile
from pathlib import Path
binary=str(Path(os.environ.get('NIFT','./nift')).resolve())
baseline=os.environ.get('NIFT_BASELINE')
with tempfile.TemporaryDirectory() as d:
 root=Path(d)
 names=['z.txt','a space.txt','é.txt','中.txt','.hidden.txt','a/deep/end.txt','b/first.txt','.private/secret.txt']
 for name in names:
  p=root/'tree'/name;p.parent.mkdir(parents=True,exist_ok=True);p.touch()
 # MSYS Python reports POSIX but its link emulation is not a native POSIX
 # filesystem link to the MinGW executable. Keep POSIX link fixtures on
 # POSIX hosts; native Windows ordering is tested without emulated links.
 posix_links = os.name!='nt' and not os.environ.get('MSYSTEM')
 if posix_links:
  (root/'tree'/'link.txt').symlink_to(root/'tree'/'z.txt')
  (root/'tree'/'dirlink').symlink_to(root/'tree'/'a',target_is_directory=True)
 native_tree=(root/'tree').as_posix()
 if os.environ.get('MSYSTEM'):
  native_tree=subprocess.check_output(['cygpath','-m',str(root/'tree')],text=True,encoding='utf-8').strip()
 patterns=['tree/*.txt','tree/**/*.txt','tree/**/**/end.txt','tree/.*.txt','tree/**/.private/*.txt','tree/missing/*.txt','tree/?/*.txt',native_tree+'/*.txt']
 path=root/'probe.f';path.write_text('\n'.join('print(ls('+json.dumps(p,ensure_ascii=False)+').stringify())' for p in patterns)+'\n',encoding='utf-8')
 p=subprocess.run([binary,str(path)],cwd=root,text=True,capture_output=True)
 assert p.returncode==0,p.stderr
 if baseline:
  q=subprocess.run([str(Path(baseline).resolve()),str(path)],cwd=root,text=True,capture_output=True)
  assert (p.returncode,p.stdout,p.stderr)==(q.returncode,q.stdout,q.stderr),(p,q)
 results=[json.loads(x) for x in p.stdout.splitlines()]
 for result in results:
  assert result==sorted(result),result
 assert results[2]==['tree/a/deep/end.txt'],results[2]
 assert results[3]==['tree/.hidden.txt'],results[3]
 assert results[4]==['tree/.private/secret.txt'],results[4]
 assert results[5]==[],results[5]
 absolute_names=[n for n in names if '/' not in n and not n.startswith('.')]
 if posix_links: absolute_names.append('link.txt')
 assert results[7]==sorted(native_tree+'/'+n for n in absolute_names),results[7]
 assert not any('/.private/' in p for p in results[1]),results[1]
 assert not any('/dirlink/' in p for p in results[1]),results[1]
 # ls resolves symlink targets after glob ordering; duplicate rendered paths
 # are existing behavior and must not be silently deduplicated here.
 print('PASS 8 glob ordering/dedup/Unicode/hidden/missing contracts'+(' plus POSIX symlinks' if posix_links else '')+(' with exact A/B' if baseline else ''))
