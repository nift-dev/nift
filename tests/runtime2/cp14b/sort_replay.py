#!/usr/bin/env python3
"""Production VM gate against frozen D4 traces, no new pre-controller runs."""
import argparse, gzip, hashlib, json, os, subprocess, tempfile
from pathlib import Path
from d4_validate import nift_source, limits

def main():
    p=argparse.ArgumentParser();p.add_argument('--sanitizer',action='store_true');p.add_argument('--nift',required=True);p.add_argument('--oracle',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    raw=Path(a.oracle).read_bytes();oracle=json.loads(gzip.decompress(raw) if a.oracle.endswith('.gz') else raw);results=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-sort-replay-') as folder:
        root=Path(folder)
        for record in oracle['results']:
            case=record['case'];source=nift_source(case);(root/'p.f').write_text(source)
            stats=root/'stats';stats.unlink(missing_ok=True)
            proc=subprocess.run([a.nift,str(root/'p.f')],env=dict(os.environ,NIFT_VM='require',NIFT_VM_STATS=str(stats),NIFT_PREVALIDATE='off'),capture_output=True,text=True,timeout=5,preexec_fn=None if a.sanitizer else limits)
            trace=[];final=None;count=None;failed=False;unexpected=[]
            for line in proc.stdout.splitlines():
                if line.startswith('T'):
                    l,r,array=map(json.loads,line[1:].split('|'));trace.append(dict(l=l,r=r,a=array))
                elif line.startswith('A'):final=json.loads(line[1:])
                elif line.startswith('N'):count=int(line[1:])
                elif line=='Estop':failed=True
                else:unexpected.append(line)
            got=dict(trace=trace,final=final,calls=count,failed=failed)
            st=[json.loads(line) for line in stats.read_text().splitlines()] if stats.exists() else []
            ok=proc.returncode==0 and not unexpected and got==record['native'] and st and st[-1]['outcome']=='vm' and st[-1]['legacy_executions']==0
            results.append(dict(case=case['id'],pass_=bool(ok),actual=got,exit=proc.returncode,stderr=proc.stderr,stats=st))
            if not ok:print('FAIL',case['id'],proc.returncode,proc.stderr[:160])
    report=dict(status='PASS' if all(r['pass_'] for r in results) else 'FAIL',binary_sha256=hashlib.sha256(Path(a.nift).read_bytes()).hexdigest(),oracle_sha256=hashlib.sha256(Path(a.oracle).read_bytes()).hexdigest(),results=results)
    Path(a.output).write_text(json.dumps(report,indent=2)+'\n');print(report['status'],len(results),'production VM traces');return 0 if report['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
