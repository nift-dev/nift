#!/usr/bin/env python3
"""Actual interpreter instruction scaling for construction-only grouping."""
import os,re,shutil,subprocess,tempfile
from pathlib import Path
if not shutil.which('valgrind'):raise SystemExit('FAIL Callgrind is required for the grouping instruction guard')
binary=str(Path(os.environ.get('NIFT','./nift')).resolve());counts=[]
with tempfile.TemporaryDirectory() as directory:
 root=Path(directory)
 for n in [1000,2000,4000]:
  source=root/'group.f';source.write_text(f'a := []\ni := 0\nwhile(i < {n}) {{ a.push(i); i += 1 }}\nb := a.group_by(x => x); print(b.size())\n')
  profile=root/'profile.callgrind';p=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(profile),binary,str(source)],capture_output=True,text=True,timeout=180)
  assert p.returncode==0 and p.stdout==f'{n}\n',(p.stdout,p.stderr)
  m=re.search(r'^summary: (\d+)',profile.read_text(),re.M);assert m,profile
  counts.append(int(m[1]));print(f'GROUP N={n}: {counts[-1]} instructions')
ratios=[b/a for a,b in zip(counts,counts[1:])]
assert all(ratio<2.6 for ratio in ratios),(counts,ratios)
print('PASS grouping instruction scaling:',ratios)
