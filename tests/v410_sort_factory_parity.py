#!/usr/bin/env python3
"""Exact accepted-state sort/factory/capture contracts, including error origins."""
import argparse
import json
import os
import sys
from pathlib import Path
import subprocess
import tempfile
from v410_oracle_paths import fixture_root_aliases, normalize_fixture_paths
sys.path.insert(0, str(Path(__file__).resolve().parent / 'runtime2'))
import approved_refusal  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--nift', default='./nift')
parser.add_argument('--baseline')
args = parser.parse_args()
binary = str(Path(args.nift).resolve())
baseline = str(Path(args.baseline).resolve()) if args.baseline else None
contract = json.loads(Path(__file__).with_name('v410_sort_factory_cases.json').read_text(encoding='utf-8'))
# Approved differences from the frozen oracle (recorded from the baseline
# commit in the cases file), each scoped to one case and one exact change.
# D-018 (feature-site-dx): an error raised in a try body is reported where it
# occurs ([2,1].sort_by at column 7), not at the try statement (1:1).
APPROVED = {
    'bad-key-cleanup': ('bad-key-cleanup.f:1:1: sort_by: key must be scalar',
                        'bad-key-cleanup.f:1:7: sort_by: key must be scalar'),
}
with tempfile.TemporaryDirectory(prefix='nift-v410-sort-oracle-') as directory:
    root = Path(directory)
    root_aliases = fixture_root_aliases(root)
    for name, source in contract['fixtures'].items():
        (root / name).write_text(source, encoding='utf-8')
    for case in contract['cases']:
        path = root / (case['name'] + '.f')
        path.write_text(case['source'] + '\n', encoding='utf-8')
        def execute(command, env=None):
            result = subprocess.run([command, str(path)], capture_output=True,
                                    text=True, encoding='utf-8', timeout=60, env=env)
            # Only the deliberate temporary fixture root differs. Preserve
            # source file, line, column, error text and all diagnostic frames.
            return (result.returncode, result.stdout,
                    normalize_fixture_paths(result.stderr, root_aliases))
        oracle = (case['exit'], case['stdout'], case['stderr'])
        expected = oracle
        if case['name'] in APPROVED:
            before, after = APPROVED[case['name']]
            if case['stderr'].count(before) != 1:
                raise SystemExit(f"FAIL {case['name']}: approved difference no longer matches the oracle")
            expected = (case['exit'], case['stdout'], case['stderr'].replace(before, after))
        actual = execute(binary)
        reason = approved_refusal.refusal_reason(actual[2])
        if actual != expected and reason in approved_refusal.APPROVED:
            # Owner 2026-11-10 13:26, item 5 (combined-b3-b5.md section 81): an approved VM-default
            # refusal (here free-name, owner ruling 2026-10-29) is pinned exactly; the recorded
            # contract is checked on the legacy channel, NIFT_VM=0.
            problem = approved_refusal.check([binary, str(path)], str(root), reason)
            if problem:
                raise SystemExit(f"FAIL {case['name']}: {problem}")
            actual = execute(binary, dict(os.environ, NIFT_VM='0'))
            print(f"PASS {case['name']}: VM default refused before execution ({reason}, "
                  f"{approved_refusal.APPROVED[reason]}); legacy channel exact")
        if actual != expected:
            raise SystemExit(f"FAIL {case['name']}: {actual!r} != {expected!r}")
        if baseline and execute(baseline) != oracle:
            raise SystemExit(f"FAIL baseline {case['name']}")
print(f"PASS {len(contract['cases'])} exact sort/factory/capture/origin contracts")
