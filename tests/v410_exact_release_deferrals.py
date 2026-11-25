"""Owner-approved v4.13.0 deferrals scoped to exact historical inputs."""
from hashlib import sha256
import json
import os
from pathlib import Path
import subprocess
import tempfile

from v410_oracle_paths import normalize_fixture_paths

LEDGER = Path(__file__).with_name('data') / 'v410-exact-release-deferrals.json'


def load_deferrals(suite, cases):
    entries = json.loads(LEDGER.read_text(encoding='utf-8'))[suite]
    missing = entries.keys() - {case['name'] for case in cases}
    if missing:
        raise SystemExit(f'Missing historical witnesses for {suite}: {sorted(missing)}')
    return entries


def valid_refusal(result, rows, first_line, reason):
    lines = result.stderr.splitlines()
    return (result.returncode == 1 and not result.stdout and lines
            and lines[0] == first_line
            and 'note: the program was not executed: no part of it ran' in lines
            and len(rows) == 1 and rows[0].get('reason') == reason
            and rows[0].get('vm_executions') == 0 and rows[0].get('legacy_executions') == 0)


def check_deferral(binary, baseline, path, case, pin, aliases):
    expected = (case['exit'], case['stdout'], case['stderr'])
    if (sha256(path.read_bytes()).hexdigest() != pin['source_sha256']
            or expected != tuple(pin['legacy'])):
        raise SystemExit(f'FAIL {case["name"]}: historical source or oracle differs from approved deferral')
    env = {k: v for k, v in os.environ.items() if k not in ('NIFT_VM', 'NIFT_VM_STATS')}
    for command, mode in [(binary, '0')] + ([(baseline, None)] if baseline else []):
        legacy_env = dict(env, NIFT_VM=mode) if mode else env
        legacy = subprocess.run([command, str(path)], env=legacy_env, capture_output=True,
                                text=True, encoding='utf-8', timeout=60)
        actual = (legacy.returncode, legacy.stdout, normalize_fixture_paths(legacy.stderr, aliases))
        if actual != expected:
            raise SystemExit(f'FAIL {case["name"]}: legacy result {actual!r} differs from {expected!r}')

    line = (f'error: <ORACLE_ROOT>/{path.name}:{pin["position"]}: not compiled for the VM '
            f'({pin["reason"]}): {pin["diagnostic"]}')
    for mode in (None, 'require'):
        with tempfile.TemporaryDirectory(prefix='nift-exact-refusal-') as directory:
            stats = Path(directory) / 'stats.jsonl'
            run_env = dict(env, NIFT_VM_STATS=str(stats))
            if mode:
                run_env['NIFT_VM'] = mode
            result = subprocess.run([binary, str(path)], env=run_env, capture_output=True,
                                    text=True, encoding='utf-8', timeout=60)
            result.stderr = normalize_fixture_paths(result.stderr, aliases)
            rows = [json.loads(row) for row in stats.read_text(encoding='utf-8').splitlines()] if stats.is_file() else []
            if not valid_refusal(result, rows, line, pin['reason']):
                raise SystemExit(f'FAIL {case["name"]} ({mode or "default"}): expected exact refusal; got {result}, {rows}')

            # The actual observation must fail validation after each independent mutation.
            for changes in ({'stdout': 'executed\n'}, {'returncode': 0},
                            {'stderr': result.stderr.replace(pin['position'], '99:99', 1)},
                            {'stderr': result.stderr.replace(pin['reason'], 'other-refusal', 1)}):
                mutant = subprocess.CompletedProcess(result.args, result.returncode, result.stdout, result.stderr)
                for key, value in changes.items():
                    setattr(mutant, key, value)
                if valid_refusal(mutant, rows, line, pin['reason']):
                    raise SystemExit(f'FAIL {case["name"]}: refusal checker accepted a changed result')
            if valid_refusal(result, [], line, pin['reason']):
                raise SystemExit(f'FAIL {case["name"]}: refusal checker accepted missing statistics')
            for counter in ('vm_executions', 'legacy_executions'):
                if valid_refusal(result, [dict(rows[0], **{counter: 1})], line, pin['reason']):
                    raise SystemExit(f'FAIL {case["name"]}: refusal checker accepted {counter}')
    print(f'PASS {case["name"]}: exact approved refusal, legacy oracle, and mutation controls')
