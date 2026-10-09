#!/usr/bin/env python3
"""Count actual interpreter and prepared-selector object traversals (no timing gate)."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--nift', required=True)
args = parser.parse_args()
binary = str(Path(args.nift).resolve())
repeats = 100
with tempfile.TemporaryDirectory(prefix='nift-object-scans-') as directory:
    path = Path(directory) / 'lookup.f'
    for width in [8,32,128]:
        literal = json.dumps({f'k{i}':i for i in range(width)},separators=(',',':'))
        for position,index in [('first',0),('middle',width//2),('last',width-1),('missing',width)]:
            for syntax in ['dot','bracket','prepared']:
                if position == 'missing' and syntax == 'prepared':
                    continue
                expression = f'o.k{index}' if syntax == 'dot' else f'o["k{index}"]'
                prefix = f'o := {literal}\n'
                if syntax == 'prepared':
                    prefix += f'f := x => x.k{index}\n'
                    expression = 'f(o)'
                body = f'total += {expression}'
                def measure(rounds):
                    path.write_text(prefix+f'i := 0\ntotal := 0\nwhile(i < {rounds}) {{ {body}; i += 1 }}\nprint(total)\n')
                    result = subprocess.run([binary,str(path)],capture_output=True,text=True,timeout=60)
                    expected = rounds if position == 'missing' else rounds*index
                    if position == 'missing' and rounds:
                        if result.returncode != 1 or result.stdout or ('has no member' not in result.stderr and 'has no key' not in result.stderr and 'invalid index' not in result.stderr):
                            raise SystemExit(f'FAIL missing error {result}')
                    elif result.returncode or result.stdout != str(expected)+'\n':
                        raise SystemExit(f'FAIL result {width}/{position}/{syntax}: {result}')
                    match = re.search(r'object-lookups scans=(\d+) comparisons=(\d+)\n$',result.stderr)
                    if not match:
                        raise SystemExit(f'FAIL instrumentation {result.stderr!r}')
                    return tuple(map(int,match.groups()))
                control = measure(0)
                actual = measure(repeats)
                delta = tuple(a-b for a,b in zip(actual,control))
                # Prepared callbacks also refresh their captured root-path binding.
                # Callgrind attributes the independent second lookup to VariableBinding::sync.
                lookups = (1 if position == 'missing' else repeats) * (2 if syntax == 'prepared' else 1)
                expected = (lookups,lookups*(width if position == 'missing' else index+1))
                if delta != expected:
                    raise SystemExit(f'FAIL scans {width}/{position}/{syntax}: {delta} != {expected}')
print('PASS one traversal per object lookup, exact comparison positions and missing checks')
