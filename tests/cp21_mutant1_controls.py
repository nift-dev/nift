"""Mutant-1 observation controls: normal/unrelated, dead, early exit, timeout."""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--normal', required=True)
p.add_argument('--unrelated', required=True)
p.add_argument('--probe', required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
rows = []
with tempfile.TemporaryDirectory(prefix='cp21-m1-controls-') as td:
    root = Path(td)
    early = root / 'early'
    early.write_text('#!/bin/sh\nprintf "~$ "\nexit 0\n')
    stalled = root / 'stalled'
    stalled.write_text('#!/bin/sh\nprintf "~$ "\nread command\nsleep 6\n')
    for f in [early, stalled]:
        f.chmod(0o755)
    for case, binary, reason in [
        ('normal', a.normal, 'expected only'),
        ('unrelated-mutant2', a.unrelated, 'expected only'),
        ('dead', '/bin/false', 'no behavioural evidence'),
        ('premature-exit', str(early), 'no behavioural evidence'),
        ('timeout', str(stalled), 'no behavioural evidence'),
    ]:
        r = subprocess.run([sys.executable, str(Path(__file__).with_name('cp21_interactive_host.py')),
                            '--probe', a.probe, binary, '--mutant', '1'],
                           text=True, capture_output=True, timeout=20)
        rows.append(dict(case=case, binary=binary, status=r.returncode, stdout=r.stdout, stderr=r.stderr))
        a.out.parent.mkdir(parents=True, exist_ok=True)
        a.out.write_text(json.dumps(rows, indent=2)+'\n')
        assert r.returncode == 1 and reason in r.stdout and 'PASS CP21 mutant 1 detected' not in r.stdout, rows[-1]
        print('PASS CP21 mutant-1 control rejected:', case)
