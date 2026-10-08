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
 if os.name!='nt':
  (root/'tree'/'link.txt').symlink_to(root/'tree'/'z.txt')
  (root/'tree'/'dirlink').symlink_to(root/'tree'/'a',target_is_directory=True)
 patterns=['tree/*.txt','tree/**/*.txt','tree/**/**/end.txt','tree/.*.txt','tree/**/.private/*.txt','tree/missing/*.txt','tree/?/*.txt',str(root/'tree'/'*.txt')]
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
 assert not any('/.private/' in p for p in results[1]),results[1]
 assert not any('/dirlink/' in p for p in results[1]),results[1]
 # ls resolves symlink targets after glob ordering; duplicate rendered paths
 # are existing behavior and must not be silently deduplicated here.
 print('PASS 8 glob ordering/dedup/Unicode/hidden/missing/symlink contracts'+(' with exact A/B' if baseline else ''))
