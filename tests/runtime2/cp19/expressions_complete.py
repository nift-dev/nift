#!/usr/bin/env python3
"""CP19 checkpoint development: shared expression coverage, never fallback credit."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ap = argparse.ArgumentParser()
ap.add_argument('--nift', required=True)
ap.add_argument('--out', type=Path, required=True)
args = ap.parse_args()
nift = str(Path(args.nift).resolve())
args.out.parent.mkdir(parents=True, exist_ok=True)
args.out.write_text('')
expressions = [
    'x := 1', '(x := [1,2])', '((x := {"a":true}))', 'f := x=>x+1',
    'x=>x+1', '[x=>x,y=>y]', '{"f":x=>x+1}',
    '[1,2].map(x=>x+1)', '[1,2,3].filter(x=>x>1)',
    '[1,2,3].reduce((a,x)=>a+x,0)', '[1].map(x=>1/0)',
    '[1,2].map("x=>x+1")', '[1].filter("x=>x>0")', '[1].map("x=>1/0")',
    '[1].map("1")', '[1].filter("true")',
    '[error("x")].stringify()', '[error("x")].prettify()',
    '{"e":error("x")}.highlight()', '{"a":[{"b":[error("deep")]}]}.prettify()',
    '[error("outer","user.outer",error("inner"))].prettify()',
    '[print("ONCE"),error("x")].stringify()',
    'error("x").cause.message', 'error("x").cause.code', 'error("x").cause.source',
    'error("outer","user.outer",error("inner")).cause.message',
    '[x=>x].stringify()', '\n  error("x").cause.message',
    '\n  [1].map(x=>1/0)', '\n  [1].map("x=>1/0")',
]
# The serialization-depth budget includes Error fields and cause objects.
for depth in [1, 20, 40]:
    expressions.append(('['*depth)+'error("x")'+(']'*depth)+'.prettify()')
count = 0
for expression in expressions:
    baseline = None
    for mode in ['0','1','require','classify','classify-all']:
        with tempfile.TemporaryDirectory(prefix='cp19-complete-expr-') as td:
            env = {k:v for k,v in os.environ.items() if not k.startswith('NIFT_')}
            stats = Path(td)/'stats'
            env.update(NIFT_VM=mode, NIFT_VM_STATS=str(stats), HOME=td)
            p = subprocess.run([nift,'eval',expression],cwd=td,env=env,capture_output=True,text=True,timeout=30)
            rows = [json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
            row = dict(expression=expression,mode=mode,exit=p.returncode,stdout=p.stdout,stderr=p.stderr,stats=rows)
            with args.out.open('a') as f:
                f.write(json.dumps(row)+'\n')
            count += 1
            if mode == '0':
                baseline = (p.returncode,p.stdout,p.stderr)
                continue
            assert len(rows) == 1, row
            s = rows[0]
            if mode in ['1','require']:
                assert (p.returncode,p.stdout,p.stderr) == baseline, (baseline,row)
                assert s['vm_executions'] == 1 and s['legacy_executions'] == 0, row
                assert s['outcome'] in ['vm','vm-error'], row
            else:
                assert p.returncode == 0 and p.stdout == p.stderr == '', row
                assert s['outcome'] == 'compiled' and s['vm_executions'] == s['legacy_executions'] == 0, row
                if mode == 'classify-all':
                    assert s['inventory_complete'] is True and s['reasons'] == [], row
print(f'PASS CP19 expression development: {count} observations')
