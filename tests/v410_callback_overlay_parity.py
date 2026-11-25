#!/usr/bin/env python3
"""Exact callback frame, capture snapshot and hydration contracts."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from v410_oracle_paths import fixture_root_aliases, normalize_fixture_paths

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--nift', default='./nift')
parser.add_argument('--baseline')
args = parser.parse_args()
contract = json.loads(Path(__file__).with_name('data').joinpath('v410-callback-overlay.json').read_text(encoding='utf-8'))
binaries = [str(Path(path).resolve()) for path in (args.nift, args.baseline) if path]
with tempfile.TemporaryDirectory(prefix='nift-v410-overlay-') as directory:
    root = Path(directory)
    aliases = fixture_root_aliases(root)
    for case in contract['cases']:
        path = root / (case['name'] + '.f')
        path.write_text(case['source'] + '\n', encoding='utf-8')
        expected = (case['exit'], case['stdout'], case['stderr'])
        checks = [(binary, None, expected) for binary in binaries]
        if 'vm_guard' in case:
            checks[0] = (binaries[0], '0', expected)
        if 'vm_stdout' in case:
            checks[0:1] = [
                (binaries[0], '0', expected),
                (binaries[0], 'require', (case['exit'], case['vm_stdout'], case['stderr'])),
            ]
        for binary, mode, expected in checks:
            env = dict(os.environ, NIFT_VM=mode) if mode else None
            result = subprocess.run([binary, str(path)], capture_output=True, env=env,
                                    text=True, encoding='utf-8', timeout=60)
            actual = (result.returncode, result.stdout,
                      normalize_fixture_paths(result.stderr, aliases))
            if actual != expected:
                raise SystemExit(f'FAIL {case["name"]} ({mode or "default"}): {actual!r} != {expected!r}')
        # D-182 preserves the reservation for the original first declaration.
        if 'vm_guard' in case:
            refusal = subprocess.run([binaries[0], str(path)], capture_output=True, text=True, encoding='utf-8',
                                     env=dict(os.environ, NIFT_VM='require'), timeout=60)
            diagnostic = normalize_fixture_paths(refusal.stderr, aliases).splitlines()
            origin = f'error: <ORACLE_ROOT>/{path.name}:{case["vm_guard"]}:'
            if (refusal.returncode != 1 or refusal.stdout or not diagnostic or not diagnostic[0].startswith(origin) or
                    'note: the program was not executed: no part of it ran' not in diagnostic):
                raise SystemExit(f'FAIL reserved binding guard {case["name"]}: {refusal}')
            path.write_text(case['vm_control_source'] + '\n', encoding='utf-8')
            for mode in ('0', 'require'):
                control = subprocess.run([binaries[0], str(path)], capture_output=True, text=True, encoding='utf-8',
                                         env=dict(os.environ, NIFT_VM=mode), timeout=60)
                if (control.returncode, control.stdout, control.stderr) != expected:
                    raise SystemExit(f'FAIL unreserved control {case["name"]} ({mode}): {control}')

            print(f'PASS {case["name"]}: existing reserved-name guard and dual-mode unreserved control')

print(f'PASS {len(contract["cases"])} exact callback environment/origin contracts')
