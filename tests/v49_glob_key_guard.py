#!/usr/bin/env python3
"""Ordering serialization scales with entries, independently of comparisons."""
import os,re,subprocess,tempfile
from pathlib import Path
binary=str(Path(os.environ.get('NIFT','./nift')).resolve())
with tempfile.TemporaryDirectory() as d:
 root=Path(d);(root/'tree').mkdir()
 for n in [100,200,400]:
  for i in range(n): (root/'tree'/f'{i:04}.txt').touch()
  source=root/'probe.f';source.write_text('print(ls("tree/*.txt").size())\n')
  p=subprocess.run([binary,str(source)],cwd=root,text=True,capture_output=True,env={**os.environ,'NIFT_TEST_GLOB_KEY_STATS':'1'})
  assert p.returncode==0 and p.stdout==f'{n}\n',(p.stdout,p.stderr)
  m=re.search(r'glob-order conversions=(\d+)',p.stderr)
  assert m and int(m[1])==n,p.stderr
  print(f'PASS {n} entries: {n} ordering-key conversions')
