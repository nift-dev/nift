#!/usr/bin/env python3
"""Exact native rejection, nested method, Unicode and diagnostic contracts.

B28 (combined-b3-b5.md section 80): the recorded results are legacy's (the authoritative
reference), so they are checked exactly on the legacy channel, NIFT_VM=0. Under the VM default
each case must give the same result, or be refused before any part of it ran (the VM-default
refusal contract, B27): exit 1, no output, and the refusal's own lines. Any other difference fails.
"""
import json,os,re,subprocess,tempfile
from pathlib import Path
binary=str(Path(os.environ.get('NIFT','./nift')).resolve())
cases=json.loads((Path(__file__).parent/'data/v410-native-dispatch.json').read_text(encoding='utf-8'))
REFUSED=re.compile(r'error: <command-line>:\d+:\d+: not compiled for the VM \([^)]+\): [^\n]*\n'
                   r'note: the program was not executed: no part of it ran\n')
refused=0
with tempfile.TemporaryDirectory() as d:
 for case in cases:
  for mode in ('0',None):
   env={k:v for k,v in os.environ.items() if k!='NIFT_VM'}
   if mode is not None:env['NIFT_VM']=mode
   # Feed UTF-8 bytes through stdin: Windows argv uses the native code page.
   p=subprocess.run([binary,'-'],input=case['source'],cwd=d,text=True,encoding='utf-8',capture_output=True,env=env)
   stderr=p.stderr.replace('error: <stdin>:', 'error: <command-line>:')
   got=[p.returncode,p.stdout,stderr]
   if mode=='0' or got==case['result']:
    assert got==case['result'],(mode,case,p)
   else:
    assert p.returncode==1 and p.stdout=='' and REFUSED.match(stderr),('VM default',case,p)
    refused+=1
print('PASS',len(cases),'exact native dispatch/method/error contracts (legacy channel); VM default: %d equal, %d refused before execution'%(len(cases)-refused,refused))
