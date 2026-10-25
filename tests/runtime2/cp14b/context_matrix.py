#!/usr/bin/env python3
"""Re-run the 272-entry built-in inventory in all six established contexts."""
import argparse,collections,json,sys,tempfile
from pathlib import Path
from functional import run
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'builtins'))
from context_probe import CASES,CONTEXTS,program

def main():
    p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--base',required=True);p.add_argument('--output',required=True);a=p.parse_args();results=[]
    with tempfile.TemporaryDirectory(prefix='cp14b-contexts-') as folder:
        root=Path(folder)
        for ordinal,(name,setup,expr) in enumerate(CASES):
            if ordinal%50==0:print("PROGRESS",ordinal,len(CASES),flush=True)
            for context in CONTEXTS:
                source=program(setup,expr,context);legacy=run(a.base,'0',source,root);base=run(a.base,'require',source,root);vm=run(a.nift,'require',source,root)
                def admitted(result):return result['stats'] and result['stats'][-1]['outcome'] in ['vm','vm-error']
                same=all(vm[k]==legacy[k] for k in ['out','err','exit']);old_same=all(base[k]==legacy[k] for k in ['out','err','exit'])
                results.append(dict(name=name,context=context,source=source,legacy=legacy,base=base,vm=vm,parity=same,old_parity=old_same,admitted=bool(admitted(vm)),base_admitted=bool(admitted(base))))
    new_refusals=[r for r in results if r['base_admitted'] and not r['admitted']]
    new_differences=[r for r in results if r['admitted'] and not r['parity'] and (r['old_parity'] or not r['base_admitted'])]
    report=dict(entries=len(CASES),contexts=len(CONTEXTS),results=results,new_refusals=len(new_refusals),new_differences=len(new_differences))
    Path(a.output).write_text(json.dumps(report,indent=2)+'\n')
    print('MATRIX',len(CASES),len(CONTEXTS),'new refusals',len(new_refusals),'new differences',len(new_differences))
    for r in new_refusals:print('NEW REFUSAL',r['name'],r['context'],r['vm']['err'])
    for r in new_differences:print('NEW DIFFERENCE',r['name'],r['context'],repr(r['legacy']['out']),repr(r['vm']['out']),repr(r['legacy']['err']),repr(r['vm']['err']))
    return 0 if not new_refusals and not new_differences else 1
if __name__=='__main__':raise SystemExit(main())
