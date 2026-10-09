#!/usr/bin/env python3
"""Exact native rejection, nested method, Unicode and diagnostic contracts."""
import json,os,subprocess,tempfile
from pathlib import Path
binary=str(Path(os.environ.get('NIFT','./nift')).resolve())
cases=json.loads((Path(__file__).parent/'data/v410-native-dispatch.json').read_text(encoding='utf-8'))
with tempfile.TemporaryDirectory() as d:
 for case in cases:
  p=subprocess.run([binary,'-e',case['source']],cwd=d,text=True,encoding='utf-8',capture_output=True)
  assert [p.returncode,p.stdout,p.stderr]==case['result'],(case,p)
print('PASS',len(cases),'exact native dispatch/method/error contracts')
