#!/usr/bin/env python3
"""Exact callback frame, capture snapshot and hydration contracts."""
import argparse
import json
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
        for binary in binaries:
            result = subprocess.run([binary, str(path)], capture_output=True,
                                    text=True, encoding='utf-8', timeout=60)
            actual = (result.returncode, result.stdout,
                      normalize_fixture_paths(result.stderr, aliases))
            expected = (case['exit'], case['stdout'], case['stderr'])
            if actual != expected:
                raise SystemExit(f'FAIL {case["name"]}: {actual!r} != {expected!r}')
print(f'PASS {len(contract["cases"])} exact callback environment/origin contracts')
