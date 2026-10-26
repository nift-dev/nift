#!/usr/bin/env python3
"""Assert the frozen review matrix; require refusals are never VM successes."""
import json
import sys
from pathlib import Path

rows = json.loads(Path(sys.argv[1]).read_text())['cases']
assert len(rows) == 135
names = set(row['name'] for row in rows)
assert len(names) == 27
for name in names:
    group = {row['mode']: row for row in rows if row['name'] == name}
    assert len(group) == 5
    baseline = group['0']
    for mode, row in group.items():
        if mode == '0':
            continue
        assert len(row['stats']) == 1, row
        stat = row['stats'][0]
        vm, legacy = stat['vm_executions'], stat['legacy_executions']
        assert vm + legacy <= 1, row
        if mode == '1' or (mode == 'require' and vm):
            assert all(row[k] == baseline[k] for k in ['exit','stdout','stderr','marker']), (baseline,row)
            if vm:
                assert stat['outcome'] in ['vm','vm-error'], row
            else:
                assert mode == '1' and legacy == 1 and stat['outcome'] == 'fallback', row
        else:
            assert vm == legacy == 0 and not row['marker'] and row['stdout'] == '', row
            if mode == 'require':
                assert row['exit'] == 1 and stat['outcome'] == 'require-error', row
            else:
                assert row['exit'] == 0 and stat['outcome'] in ['compiled','fallback'], row
print('PASS original reviewer matrix: 135 observations, exact admitted parity and pre-effect refusal accounting')
