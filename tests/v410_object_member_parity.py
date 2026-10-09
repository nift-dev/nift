#!/usr/bin/env python3
"""Exact accepted-state object-member/value/location and error contracts."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--nift', default='./nift')
parser.add_argument('--baseline')
args = parser.parse_args()
binary = str(Path(args.nift).resolve())
baseline = str(Path(args.baseline).resolve()) if args.baseline else None
contract = json.loads(Path(__file__).with_name('v410_object_member_cases.json').read_text(encoding='utf-8'))
with tempfile.TemporaryDirectory(prefix='nift-v410-object-oracle-') as directory:
    root = Path(directory)
    for name, source in contract['fixtures'].items():
        (root / name).write_text(source, encoding='utf-8')
    for case in contract['cases']:
        path = root / (case['name'] + '.f')
        path.write_text(case['source'] + '\n', encoding='utf-8')
        def execute(command):
            result = subprocess.run([command, str(path)], capture_output=True,
                                    text=True, encoding='utf-8', timeout=60)
            # Only the deliberate temporary fixture root differs. Preserve
            # source file, line, column, error text and all diagnostic frames.
            stderr = result.stderr
            # Windows may render the fixture prefix with native separators.
            # Normalize only that exact prefix, never filenames or diagnostics.
            for prefix in (str(root) + '\\', root.as_posix() + '/', str(root) + '/'):
                stderr = stderr.replace(prefix, '<ORACLE_ROOT>/')
            return (result.returncode, result.stdout,
                    stderr.replace(str(root), '<ORACLE_ROOT>'))
        expected = (case['exit'], case['stdout'], case['stderr'])
        actual = execute(binary)
        if actual != expected:
            raise SystemExit(f"FAIL {case['name']}: {actual!r} != {expected!r}")
        if baseline and execute(baseline) != expected:
            raise SystemExit(f"FAIL baseline {case['name']}")
print(f"PASS {len(contract['cases'])} exact object/member/location/origin contracts")
