#!/usr/bin/env python3
"""Exact accepted-state object-member/value/location and error contracts."""
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
binary = str(Path(args.nift).resolve())
baseline = str(Path(args.baseline).resolve()) if args.baseline else None
contract = json.loads(Path(__file__).with_name('v410_object_member_cases.json').read_text(encoding='utf-8'))
with tempfile.TemporaryDirectory(prefix='nift-v410-object-oracle-') as directory:
    root = Path(directory)
    root_aliases = fixture_root_aliases(root)
    for name, source in contract['fixtures'].items():
        (root / name).write_text(source, encoding='utf-8')
    for case in contract['cases']:
        path = root / (case['name'] + '.f')
        path.write_text(case['source'] + '\n', encoding='utf-8')
        def execute(command, mode=None):
            env = dict(os.environ, NIFT_VM=mode) if mode else None
            result = subprocess.run([command, str(path)], capture_output=True, env=env,
                                    text=True, encoding='utf-8', timeout=60)
            # Only the deliberate temporary fixture root differs. Preserve
            # source file, line, column, error text and all diagnostic frames.
            return (result.returncode, result.stdout,
                    normalize_fixture_paths(result.stderr, root_aliases))
        expected = (case['exit'], case['stdout'], case['stderr'])
        if 'vm_stdout' in case:
            if execute(binary, '0') != expected:
                raise SystemExit(f"FAIL legacy {case['name']}")
            vm_expected = (case['exit'], case['vm_stdout'], case['stderr'])
        else:
            vm_expected = expected
        actual = execute(binary, 'require' if 'vm_stdout' in case else None)
        if actual != vm_expected:
            raise SystemExit(f"FAIL {case['name']}: {actual!r} != {vm_expected!r}")
        if baseline and execute(baseline) != expected:
            raise SystemExit(f"FAIL baseline {case['name']}")
print(f"PASS {len(contract['cases'])} exact object/member/location/origin contracts")
