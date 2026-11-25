#!/usr/bin/env python3
"""Exact callback frame, capture snapshot and hydration contracts."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from v410_oracle_paths import fixture_root_aliases, normalize_fixture_paths
from v410_exact_release_deferrals import check_deferral, load_deferrals
sys.path.insert(0, str(Path(__file__).resolve().parent / 'runtime2'))
import approved_refusal  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--nift', default='./nift')
parser.add_argument('--baseline')
args = parser.parse_args()
contract = json.loads(Path(__file__).with_name('data').joinpath('v410-callback-overlay.json').read_text(encoding='utf-8'))
binaries = [str(Path(path).resolve()) for path in (args.nift, args.baseline) if path]
# D-065 defers timer construction. Keep both legacy capture diagnostics exact.
timer_cases = {'unused-timer', 'param-timer'}
deferrals = load_deferrals('v410-callback-overlay', contract["cases"])
with tempfile.TemporaryDirectory(prefix='nift-v410-overlay-') as directory:
    root = Path(directory)
    aliases = fixture_root_aliases(root)
    for case in contract['cases']:
        path = root / (case['name'] + '.f')
        path.write_bytes((case['source'] + '\n').encode('utf-8'))
        if case['name'] in deferrals:
            check_deferral(binaries[0], binaries[1] if len(binaries) > 1 else None, path, case, deferrals[case['name']], aliases)
            continue
        expected = (case['exit'], case['stdout'], case['stderr'])
        checks = [(binary, None, expected) for binary in binaries]
        if 'vm_guard' in case:
            checks[0] = (binaries[0], '0', expected)
        if 'vm_stdout' in case:
            # B5 D5 retains the entry snapshot for a local synchronous mapper.
            vm_expected = (case.get('vm_exit', case['exit']), case['vm_stdout'],
                           case.get('vm_stderr', case['stderr']))
            checks[0:1] = [
                (binaries[0], '0', expected),
                (binaries[0], 'require', vm_expected),
            ]
        if case['name'] in timer_cases:
            def legacy_problem(result):
                actual = (result.returncode, result.stdout, normalize_fixture_paths(result.stderr, aliases))
                return None if actual == expected else repr(actual)

            problem = approved_refusal.check(case['name'], [binaries[0], str(path)], str(root),
                                             'callable:timer', legacy_problem)
            if problem:
                raise SystemExit(problem)
            vm = subprocess.run([binaries[0], str(path)], capture_output=True, text=True, encoding='utf-8',
                                env=dict(os.environ, NIFT_VM='require'), timeout=60)
            origin = f'error: <ORACLE_ROOT>/{path.name}:1:6: not compiled for the VM (callable:timer):'
            if (vm.returncode != 1 or vm.stdout or 'no part of it ran' not in vm.stderr or
                    not normalize_fixture_paths(vm.stderr, aliases).startswith(origin)):
                raise SystemExit(f'FAIL timer refusal location {case["name"]}: {vm}')
            checks = checks[1:]
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
