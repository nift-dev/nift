#!/usr/bin/env python3
"""Dependency time/error/alias contracts through ordinary incremental builds."""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from shared_dependency_targeted_reproducer import setup, run

parser = argparse.ArgumentParser()
parser.add_argument('--nift', required=True)
parser.add_argument('--output')
args = parser.parse_args()
exe = str(pathlib.Path(args.nift).resolve())
rows = []
cases = ['older', 'equal', 'newer', 'deleted-recreated', 'denied-parent',
         'unreadable', 'alias', 'escaping', 'broken', 'pre-hook']
with tempfile.TemporaryDirectory(prefix='nift-status-contract-') as td:
    for mode in ['modified', 'hash', 'hybrid']:
        for case in cases:
            if os.name == 'nt' and case in ['denied-parent', 'unreadable']:
                rows.append({'mode': mode, 'case': case, 'skipped': 'POSIX permission fixture'})
                continue
            p = pathlib.Path(td) / (mode + '-' + case)
            p.mkdir()
            dep, _, _ = setup(p, mode, 'explicit')
            if case == 'pre-hook':
                config_path = p / '.nift/config.json'
                config = json.loads(config_path.read_text())
                config['config']['pre build'] = 'pre.f'
                config_path.write_text(json.dumps(config))
                (p / 'gate').write_text('OLD')
                (p / 'hook.py').write_text(
                    'from pathlib import Path\n'
                    f'Path({str(dep.relative_to(p))!r}).write_text(Path("gate").read_text())\n')
                (p / 'pre.f').write_text('r := run(' + json.dumps(sys.executable) + ', "hook.py")\n')
            run(exe, p, 'build', '--all')
            stamp = (p / '.nift/public/a.info.json').stat().st_mtime_ns
            if case in ['older', 'equal', 'newer']:
                dep.write_text('NEW')
                ns = stamp + {'older': -1000000000, 'equal': 0, 'newer': 1000000000}[case]
                os.utime(dep, ns=(ns, ns))
                run(exe, p, 'build')
                out = (p / 'public/a.html').read_text()
                assert ('NEW' in out) == (case != 'older' or mode != 'modified'), (mode, case, out)
            elif case == 'deleted-recreated':
                dep.unlink()
                result = subprocess.run([exe, 'build'], cwd=p, capture_output=True)
                assert result.returncode != 0
                dep.write_text('RECREATED')
                run(exe, p, 'build')
                assert 'RECREATED' in (p / 'public/a.html').read_text()
            elif case in ['denied-parent', 'unreadable']:
                if case == 'unreadable':
                    dep.write_text('CHANGED')
                target = dep.parent if case == 'denied-parent' else dep
                perms = target.stat().st_mode
                target.chmod(0)
                try:
                    result = subprocess.run([exe, 'build'], cwd=p, capture_output=True)
                finally:
                    target.chmod(perms)
                assert result.returncode != 0, (mode, case)
            elif case == 'pre-hook':
                # No dependency edit until the project hook runs. A status from
                # an earlier phase must not conceal the post-hook mutation.
                (p / 'gate').write_text('POST-HOOK')
                run(exe, p, 'build')
                assert 'POST-HOOK' in (p / 'public/a.html').read_text(), mode
            else:
                dep.unlink()
                target = p / 'data/target'
                target.write_text('ALIAS')
                link = target if case == 'alias' else pathlib.Path(td) / 'external' if case == 'escaping' else p / 'data/absent'
                if case == 'escaping':
                    link.write_text('ESCAPED')
                try:
                    dep.symlink_to(link)
                except OSError as error:
                    if os.name != 'nt':
                        raise
                    rows.append({'mode': mode, 'case': case, 'skipped': str(error)})
                    continue
                result = subprocess.run([exe, 'build'], cwd=p, capture_output=True)
                assert (result.returncode == 0) == (case == 'alias'), (mode, case, result.stdout, result.stderr)
                if case == 'alias':
                    assert 'ALIAS' in (p / 'public/a.html').read_text()
            rows.append({'mode': mode, 'case': case, 'passed': True})
if args.output:
    pathlib.Path(args.output).write_text(json.dumps(rows, indent=2) + '\n')
print('CLI status adversarial PASS', sum(row.get('passed', False) for row in rows),
      'SKIP', sum('skipped' in row for row in rows))
