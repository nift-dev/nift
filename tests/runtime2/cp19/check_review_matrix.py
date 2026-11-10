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
            # B28 (combined-b3-b5.md section 80): the one approved difference. A function has no shown
            # form on the VM (b4-dispatch.md 6, owner: no internal tag): where legacy prints its internal
            # callable tag, the VM gives stringify's ordinary error, with the same effects (marker).
            tagged = '\\u001fnift:callable:' in baseline['stdout'] or '\x1fnift:callable:' in baseline['stdout']
            approved = (tagged and vm and (row['exit'], row['stdout'], row['stderr']) ==
                        (2, '', 'eval: opaque runtime value is not serializable\n') and row['marker'] == baseline['marker'])
            assert approved or all(row[k] == baseline[k] for k in ['exit','stdout','stderr','marker']), (baseline,row)
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
