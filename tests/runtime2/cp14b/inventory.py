#!/usr/bin/env python3
"""Exact original 89-probe admission delta, with D-040 and safety debt explicit."""
import argparse,collections,hashlib,json,tempfile
from pathlib import Path
from functional import run
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--base',required=True);p.add_argument('--probes',required=True);p.add_argument('--output',required=True);a=p.parse_args()
cases=json.loads(Path(a.probes).read_text())['cases'];results=[]
with tempfile.TemporaryDirectory(prefix='cp14b-inventory-') as folder:
 root=Path(folder)
 for c in cases:
  source=c['source'];vm=run(a.nift,'require',source,root);old=run(a.base,'require',source,root)
  refusal=bool(vm['stats'] and vm['stats'][-1]['outcome']=='require-error');legacy=None
  if c['id']=='sort.self_push_bounded':
   classification='self-structural';ok=refusal and not vm['out'] and vm['stats'][-1]['vm_executions']==0
  else:
   legacy=run(a.base,'0',source,root);same=all(vm[k]==legacy[k] for k in ['out','err','exit'])
   if c['id']=='alias.copy':
    classification='D-040 independent copy';ok=vm['exit']==0 and vm['out']=='[1,2]\n[1,2,9]\n'
   elif c['id']=='nested.mutate':classification='receiver-shape debt';ok=refusal
   else:classification='exact parity';ok=same and not refusal
  results.append(dict(id=c['id'],source=source,classification=classification,pass_=bool(ok),legacy=legacy,base=old,candidate=vm))
  print('PASS' if ok else 'FAIL',c['id'],classification)
report=dict(status='PASS' if all(r['pass_'] for r in results) else 'FAIL',binary_sha256=hashlib.sha256(Path(a.nift).read_bytes()).hexdigest(),counts=dict(collections.Counter(r['classification'] for r in results)),results=results)
Path(a.output).write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(0 if report['status']=='PASS' else 1)
