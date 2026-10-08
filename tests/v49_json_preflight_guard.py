#!/usr/bin/env python3
import os,subprocess,re
from pathlib import Path
binary=str(Path(os.environ['NIFT_JSON_GUARD']).resolve())
for n in [100,200,400]:
 p=subprocess.run([binary,str(n)],capture_output=True,text=True,env={**os.environ,'NIFT_TEST_JSON_PREFLIGHT_STATS':'1'})
 assert p.returncode==0 and p.stdout==f'{n}\n',(p.stdout,p.stderr)
 m=re.search(r'json-conversion preflights=(\d+) conversions=(\d+)',p.stderr)
 assert m and tuple(map(int,m.groups()))==(1,n+49),p.stderr
 print(f'PASS {n} leaves, depth 48: one preflight, {n+49} converted nodes')
