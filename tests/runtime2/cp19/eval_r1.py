#!/usr/bin/env python3
"""CP19 R1: admitted parity, conservative pre-effect refusals, real prefix mutant."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--expect-return-prefix', action='store_true')
    args = ap.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text('')
    binary = str(Path(args.nift).resolve())
    count = 0

    def run(expression, mode, script=False):
        nonlocal count
        with tempfile.TemporaryDirectory(prefix='cp19-r1-') as td:
            root = Path(td)
            env = {k: v for k, v in os.environ.items() if not k.startswith('NIFT_')}
            env.update(NIFT_VM=mode, NIFT_VM_STATS=str(root/'stats'), HOME=td)
            p = subprocess.run([binary, '-c' if script else 'eval', expression], cwd=root,
                               env=env, capture_output=True, text=True, timeout=30)
            stats = root/'stats'
            rows = [json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
            row = dict(expression=expression, mode=mode, script=script, exit=p.returncode,
                       stdout=p.stdout, stderr=p.stderr, stats=rows, marker=(root/'marker').exists())
            with args.out.open('a') as file:
                file.write(json.dumps(row)+'\n')
            count += 1
            return row

    def channels(row):
        return row['exit'], row['stdout'], row['stderr'], row['marker']

    def execution(row, outcome):
        assert len(row['stats']) == 1, row
        s = row['stats'][0]
        assert s['outcome'] == outcome, row
        assert (s['vm_executions'], s['legacy_executions']) == (1, 0), row
        return s

    expected = 'eval: transformation: callback must be callable\n'
    probe = run('[1].map(1)', 'require')
    execution(probe, 'vm-error')
    if args.expect_return_prefix:
        assert probe['exit'] == 2 and probe['stdout'] == '', probe
        assert probe['stderr'] == 'eval: return: transformation: callback must be callable\n', probe
        assert probe['stderr'] != expected
        print('PASS real synthetic-return-prefix mutant detected by exact diagnostic gate')
        return
    assert probe['stderr'] == expected, probe

    admitted = [
        '[1,2].stringify()', '{"a":[1,true]}.prettify()', '{"a":[1]}.highlight()',
        'error("x").stringify()', 'error("x").prettify()', 'error("x").highlight()',
        'error("x").cause.stringify()', 'error("x").cause.prettify()',
        'error("x").cause.highlight()',
        'error("outer","user.outer",error("inner")).cause.stringify()',
        'error("outer","user.outer",error("inner")).cause.prettify()',
        'error("outer","user.outer",error("inner")).cause.highlight()',
        'error("x").message', 'error("x").code', 'error("x").source',
        'error("x").line', 'error("x").column', '[error("x")]',
    ] + [f'[1].{method}({value})' for method, value in [
        ('map','1'), ('filter','true'), ('find','null'), ('sort_by','null'),
        ('find_index','false'), ('group_by','null')]]
    for expression in admitted:
        baseline = run(expression, '0')
        for mode in ['1','require']:
            row = run(expression, mode)
            assert channels(row) == channels(baseline), (baseline, row)
            execution(row, 'vm-error' if row['exit'] and expression != '[error("x")]' else 'vm')

    refused = [
        '[error("x")].stringify()', '[error("x")].prettify()', '{"e":error("x")}.highlight()',
        '{"a":[{"b":[error("deep")]}]}.prettify()',
        '[[error("x")]].stringify()', '[[error("x")]].highlight()',
        '[print("ONCE"),error("x")].stringify()',
        '[touch("marker"),error("x")].prettify()',
        '{"a":[touch("marker"),error("x")]}.highlight()',
        '[1].map("x")', '[touch("marker"),[1].map("x")]',
        '[{"a":2},{"a":1}].sort_by("a")',
    ] + [f'error("x").cause.{member}' for member in ['message','code','source']] + [
        f'error("outer","user.outer",error("inner")).cause.{member}'
        for member in ['message','code','source']] + [
        '[touch("marker"),error("x").cause.message]',
        '[print("ONCE"),error("x").cause.code]',
        '\n  error("x").cause.message',
    ]
    for expression in refused:
        baseline = run(expression, '0')
        row = run(expression, '1')
        assert channels(row) == channels(baseline), (baseline, row)
        # Complete CP19 promotes the previously conservative R1 cases to
        # genuine VM execution. Quoted unresolved names remain refusals.
        if '.map("x")' not in expression and '.sort_by("a")' not in expression:
            execution(row, 'vm-error' if row['exit'] else 'vm')
            required = run(expression, 'require')
            assert channels(required) == channels(baseline), (baseline,required)
            execution(required, 'vm-error' if required['exit'] else 'vm')
            for mode in ['classify','classify-all']:
                classified = run(expression, mode)
                assert classified['exit'] == 0 and classified['stdout'] == '' and not classified['marker'], classified
                assert len(classified['stats']) == 1, classified
                stat = classified['stats'][0]
                assert stat['outcome'] == 'compiled' and stat['vm_executions'] == stat['legacy_executions'] == 0, classified
            continue
        assert len(row['stats']) == 1, row
        assert (row['stats'][0]['vm_executions'], row['stats'][0]['legacy_executions']) == (0,1), row
        assert row['stats'][0]['outcome'] == 'fallback', row
        for mode in ['require','classify','classify-all']:
            row = run(expression, mode)
            assert row['stdout'] == '' and not row['marker'], row
            assert len(row['stats']) == 1, row
            s = row['stats'][0]
            assert (s['vm_executions'], s['legacy_executions']) == (0,0), row
            assert s['outcome'] == ('require-error' if mode == 'require' else 'fallback'), row
            if expression.startswith('\n  '):
                assert (s['line'],s['column']) == (2,3) and s['path'] == '<eval>', row
            assert row['exit'] == (1 if mode == 'require' else 0), row

    row = run('\n  [1].map(1)', 'require')
    s = execution(row, 'vm-error')
    assert row['stderr'] == expected and (s['line'],s['column']) == (2,3), row
    # Real script/function return contexts must retain their legacy projection.
    for source in ['return [1].map(1).sum()', 'fn(f()){ return [1].map(1) }\nx := f()']:
        baseline = run(source, '0', True)
        row = run(source, 'require', True)
        assert channels(row) == channels(baseline), (baseline,row)
        execution(row, 'vm-error')
        assert 'return:' in row['stderr'], row
    print(f'PASS R1 parity/refusal/return-context: {count} observations')


if __name__ == '__main__':
    main()
