#!/usr/bin/env python3
"""Exact native rejection, nested method, Unicode and diagnostic contracts.

B28 (combined-b3-b5.md section 80): the recorded results are legacy's (the authoritative
reference), so they are checked exactly on the legacy channel, NIFT_VM=0.
Review of C83-C99, P1-1 (section 89): under the VM default each case must give the recorded result,
or be refused before any part of it ran with the reason pinned for that case below. A pinned refusal
is accepted only when its CP25-R inventory status (classification.tsv, via approved_refusal.py) is
approved or deferred-named; a PENDING-OWNER reason, one with no row, an unpinned refusal or a
different reason fails, naming the case and the reason.
"""
import json,os,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent/'runtime2'))
import approved_refusal
binary=str(Path(os.environ.get('NIFT','./nift')).resolve())
cases=json.loads((Path(__file__).parent/'data/v410-native-dispatch.json').read_text(encoding='utf-8'))
# The VM-default refusal expected for each case that does not give legacy's recorded result.
EXPECTED_REFUSAL={
 'print(len("abc"))':'callable:len',
 'print( len("abc") )':'callable:len',
 'print(to_string(1))':'callable:to_string',
 'print(unknown(1))':'callable:unknown',
 'print(print())':'static-error:arity',
}
failures=[];refused=0
with tempfile.TemporaryDirectory() as d:
 for case in cases:
  source=case['source']
  for mode in ('0',None):
   env={k:v for k,v in os.environ.items() if k!='NIFT_VM'}
   if mode is not None:env['NIFT_VM']=mode
   # Feed UTF-8 bytes through stdin: Windows argv uses the native code page.
   p=subprocess.run([binary,'-'],input=source,cwd=d,text=True,encoding='utf-8',capture_output=True,env=env)
   stderr=p.stderr.replace('error: <stdin>:', 'error: <command-line>:')
   got=[p.returncode,p.stdout,stderr]
   if mode=='0':
    if got!=case['result']:failures.append('%r: legacy channel: %r != %r'%(source,got,case['result']))
   elif got!=case['result']:
    reason=approved_refusal.refusal_reason(stderr)
    pinned=EXPECTED_REFUSAL.get(source)
    if pinned is None:failures.append('%r: VM default differs and no refusal is pinned for it: %r'%(source,got));continue
    if reason!=pinned:failures.append('%r: VM default refused with %r, pinned %s: %r'%(source,reason,pinned,got));continue
    problem=approved_refusal.check(source,[binary,'-'],d,pinned,tuple(case['result'][:2]),stdin=source)
    if problem:failures.append(problem)
    else:refused+=1
for f in failures:print('FAIL',f)
if failures:sys.exit('FAIL native dispatch: %d of %d case(s)'%(len(failures),len(cases)))
print('PASS',len(cases),'exact native dispatch/method/error contracts (legacy channel); VM default: %d equal, %d accepted refusals'%(len(cases)-refused,refused))
