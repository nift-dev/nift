#!/usr/bin/env python3
"""Exact byte-string replacement, overlap, size changes and error contracts."""
import json,os,subprocess,tempfile
from pathlib import Path
binary=str(Path(os.environ.get('NIFT','./nift')).resolve())
cases=json.loads((Path(__file__).parent/'data/v410-string-replace.json').read_text(encoding='utf-8'))
with tempfile.TemporaryDirectory() as d:
 for case in cases:
  # Feed UTF-8 bytes through stdin: Windows argv uses the native code page.
  p=subprocess.run([binary,'-'],input=case['source'],cwd=d,text=True,encoding='utf-8',capture_output=True)
  stderr=p.stderr.replace('error: <stdin>:', 'error: <command-line>:')
  assert [p.returncode,p.stdout,stderr]==case['result'],(case,p)
print('PASS',len(cases),'exact byte-string replacement contracts')
