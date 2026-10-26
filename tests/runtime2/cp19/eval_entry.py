#!/usr/bin/env python3
"""CP19 shared expression entry: parity is necessary, VM evidence mandatory."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--nift', required=True)
    parser.add_argument('--expect-bypass', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    nift = str(Path(args.nift).resolve())
    records = []
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text('', encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='cp19-eval-') as tmp:
        root = Path(tmp)
        stats = root / 'stats.jsonl'
        (root / 'input.txt').write_text('host-content', encoding='utf-8')

        def run(argv, mode, stdin=None, knobs=None):
            stats.write_text('', encoding='utf-8')
            env = {k: v for k, v in os.environ.items() if not k.startswith('NIFT_')}
            env.update(NIFT_VM=mode, NIFT_VM_STATS=str(stats), HOME=tmp)
            env.update(knobs or {})
            result = subprocess.run([nift, *argv], input=stdin, cwd=root, env=env,
                                    capture_output=True, text=True, timeout=30)
            rows = [json.loads(line) for line in stats.read_text().splitlines()]
            records.append(dict(argv=argv, mode=mode, rc=result.returncode,
                                stdout=result.stdout, stderr=result.stderr, rows=rows))
            if args.report:
                with args.report.open('a', encoding='utf-8') as file:
                    file.write(json.dumps(records[-1]) + '\n')
            return (result.returncode, result.stdout, result.stderr), rows

        def executed(rows, outcome='vm'):
            assert len(rows) == 1, rows
            row = rows[0]
            assert row['outcome'] == outcome, row
            assert row['vm_executions'] == 1 and row['legacy_executions'] == 0, row
            return row

        # This is the ordinary positive gate, run unchanged on the real mutant.
        legacy, no_rows = run(['eval', '1+2'], '0')
        actual, rows = run(['eval', '1+2'], 'require')
        assert legacy == actual == (0, '3\n', '')
        assert no_rows == []
        if args.expect_bypass:
            try:
                executed(rows)
            except AssertionError:
                assert rows == [], rows  # precise missing-route signature
                print('PASS compiled legacy-bypass mutant detected: correct output without VM execution')
                return
            raise AssertionError('legacy-bypass mutant survived affirmative execution gate')
        executed(rows)

        expressions = [
            'x := 1', '[1,2].map(x => x+1)', 'null', 'true', 'false', '1+2*3', '-2.5+1', 'true ? 3 : 4',
            'null ?? "fallback"', '"hello".to_upper()', '""',
            '"quote\\\"\\ntext"', '"a"+"b"', '[1,2,3]',
            '{"a":1,"b":[true,null,"x"]}',
            '[1,2,2].unique()', '[1,[2,3]].flatten()', '[1,2,3].sum()',
            '{"x":1,"y":2}.keys()', '{"x":1}.get("x")',
            'pwd()', 'open("input.txt")', 'exists("input.txt")',
            'getenv("CP19_NOT_SET")', 'print("EXACTLY-ONCE")',
            'setenv("NIFT_VM","classify")',
            'error("unserializable")', '[error("nested")]',
            'error("x").source', 'error("x").line', 'error("x").column',
            'error("outer","user.outer",error("cause")).stringify()',
        ]
        for expression in expressions:
            for flags in ([], ['--json']):
                argv = ['eval', *flags, expression]
                expected, _ = run(argv, '0')
                for mode in ('1', 'require'):
                    actual, rows = run(argv, mode)
                    assert actual == expected, (argv, mode, expected, actual, rows)
                    executed(rows)

        # Runtime errors execute once; the CLI keeps eval's diagnostic/exit format.
        for expression in ['1/0', 'open("absent.txt")', '[1][9]', 'error(1)',
                           'error("x","bad")', '[print("BEFORE-ERROR"), 1/0]']:
            expected, _ = run(['eval', expression], '0')
            actual, rows = run(['eval', expression], 'require')
            assert actual == expected and actual[0] == 2, (expression, expected, actual, rows)
            executed(rows, 'vm-error')

        # Resource/internal stops must not replay an expression on legacy.
        actual, rows = run(['eval', '[1,2,3]'], 'require',
                           knobs={'NIFT_VM_HEAP_BUDGET': '1'})
        assert actual[0] == 70 and 'memory budget exceeded' in actual[2], actual
        executed(rows, 'internal-error')

        # No source wrapping: statistics retain the expression's byte positions.
        actual, rows = run(['eval', '\n  1 / 0'], 'require')
        row = executed(rows, 'vm-error')
        assert actual == (2, '', 'eval: division by zero\n'), actual
        assert row['path'] == '<eval>' and (row['line'], row['column']) == (2, 3), row

        # Unsupported expression units select legacy BEFORE any evaluation.
        # They are never called VM coverage, and require never evaluates them.
        for expression in ['missing', '1 +',
                           '[3,1,2].sort()', 'bytes([65,66])', '{"nested":bytes([65])}',
                           'timer()', 'bytes("hi")']:
            expected, _ = run(['eval', expression], '0')
            actual, rows = run(['eval', expression], '1')
            assert actual == expected, (expression, expected, actual)
            assert len(rows) == 1 and rows[0]['outcome'] == 'fallback', rows
            assert (rows[0]['vm_executions'], rows[0]['legacy_executions']) == (0, 1), rows
            refused, rows = run(['eval', expression], 'require')
            assert refused[0] == 1 and 'NIFT_VM=require' in refused[2], refused
            assert rows[0]['outcome'] == 'require-error', rows
            assert (rows[0]['vm_executions'], rows[0]['legacy_executions']) == (0, 0), rows

        # Host side effects inside a refused unit must not occur during classification.
        expression = '[touch("marker"), bytes([65])]'
        for mode in ('require', 'classify', 'classify-all'):
            result, rows = run(['eval', expression], mode)
            assert not (root / 'marker').exists(), (mode, result, rows)
            assert rows[0]['vm_executions'] == rows[0]['legacy_executions'] == 0, rows
        result, rows = run(['eval', expression], '1')
        assert (root / 'marker').exists(), (result, rows)
        assert rows[0]['outcome'] == 'fallback' and rows[0]['legacy_executions'] == 1, rows

        for mode in ('classify', 'classify-all'):
            result, rows = run(['eval', 'print("MUST-NOT-PRINT")'], mode)
            assert result == (0, '', ''), result
            assert rows[0]['outcome'] == 'compiled', rows
            assert rows[0]['vm_executions'] == rows[0]['legacy_executions'] == 0, rows
            if mode == 'classify-all':
                assert rows[0]['inventory_complete'] is True and rows[0]['reasons'] == [], rows

        # Existing standalone entries still share the same runner and source sites.
        script = 'print("SCRIPT-VM")'
        path = root / 'unit.f'
        path.write_text(script, encoding='utf-8')
        for argv, stdin in [(['-c', script], None), ([str(path)], None), (['-'], script)]:
            expected, _ = run(argv, '0', stdin)
            actual, rows = run(argv, 'require', stdin)
            assert actual == expected == (0, 'SCRIPT-VM\n', ''), (argv, actual, expected)
            executed(rows)
        # Native command-form remains an external shell command, not a script unit.
        if os.name == 'posix':
            result, rows = run([], 'require', '/bin/echo NATIVE-COMMAND\nexit\n')
            assert result[0] == 0 and 'NATIVE-COMMAND\n' in result[1], result
            assert rows == [], rows
        # Capabilities and invalid CLI arity never become evaluated expressions.
        for argv in [['eval', '--capabilities'], ['eval'], ['eval', '1', '2']]:
            expected, _ = run(argv, '0')
            actual, rows = run(argv, 'require')
            assert actual == expected and rows == [], (argv, actual, expected, rows)

    print(f'PASS CP19 expression parity/routing: {len(records)} process observations')


if __name__ == '__main__':
    main()
