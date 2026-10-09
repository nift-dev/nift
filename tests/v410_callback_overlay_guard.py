#!/usr/bin/env python3
"""Check actual callback hydration and missing-name paths without timing thresholds."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

binary = str(Path(os.environ['NIFT']).resolve())

def balanced(name, count):
    if count == 1:
        return name
    left = count // 2
    return '(' + balanced(name, left) + '+' + balanced(name, count-left) + ')'

cases = [
    ('parameter', 'f := x => x; print([1,2,3].map(f).join(","))',
     '1,2,3\n', dict(overlay_hits=3, capture_hits=0, hydrations=0, misses=0, fallbacks=0)),
    ('hydrate once per frame', 'v := 7; f := x => x+x+v+v; print([1,2,3].map(f).join(","))',
     '16,18,20\n', dict(overlay_hits=9, capture_hits=3, hydrations=3, misses=0, fallbacks=0)),
]
for count in (1, 8, 32, 128, 256, 1024):
    # Membership excludes m at creation. Larger plans retain the existing
    # 64-node preparation guard and materialize before compatibility evaluation.
    source = 'f := x => ' + balanced('m', count) + '; m := 7; print([1,2,3].map(f).join(","))'
    misses = 3*count if count <= 32 else 0
    cases.append((f'missing x{count}', source, ','.join([str(7*count)]*3)+'\n',
                  dict(overlay_hits=0, capture_hits=0, hydrations=0, misses=misses, fallbacks=misses)))
with tempfile.TemporaryDirectory(prefix='nift-callback-overlay-') as directory:
    path = Path(directory) / 'probe.f'
    for name, source, expected, counters in cases:
        path.write_text(source+'\n', encoding='utf-8')
        result = subprocess.run([binary, str(path)], capture_output=True, text=True,
                                encoding='utf-8', timeout=60,
                                env={**os.environ, 'NIFT_TEST_LAMBDA_CACHE_STATS': '1'})
        if result.returncode or result.stdout != expected:
            raise SystemExit(f'FAIL {name}: {result.stdout!r}, {result.stderr!r}')
        match = re.search(r'^callback-overlay (.*)$', result.stderr, re.MULTILINE)
        if not match:
            raise SystemExit(f'FAIL {name}: missing counters')
        actual = {key:int(value) for key,value in re.findall(r'(\w+)=(\d+)', match[1])}
        if actual != counters:
            raise SystemExit(f'FAIL {name}: {actual} != {counters}')
        print(f'PASS {name}: {actual}')
