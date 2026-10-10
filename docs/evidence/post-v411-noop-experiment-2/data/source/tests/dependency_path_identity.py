#!/usr/bin/env python3
"""Lexical spellings and containment through saved and current dependency checks."""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile
from shared_dependency_targeted_reproducer import setup, run, output_tree

parser = argparse.ArgumentParser()
parser.add_argument('--nift', required=True)
parser.add_argument('--baseline')
parser.add_argument('--output')
args = parser.parse_args()
binaries = {'candidate': str(pathlib.Path(args.nift).resolve())}
if args.baseline:
    binaries['baseline'] = str(pathlib.Path(args.baseline).resolve())
spellings = ['data/shared.html', 'data/./shared.html', './data/shared.html',
             'data//shared.html', 'data/nested/../shared.html']
if os.name == 'nt':
    spellings.append('data\\shared.html')
rows = []
with tempfile.TemporaryDirectory(prefix='nift-path-identity-') as td:
    for mode in ['modified', 'hash', 'hybrid']:
        for spelling in spellings:
            results = []
            for label, binary in binaries.items():
                p = pathlib.Path(td) / f'{mode}-{len(rows)}-{label}'
                p.mkdir()
                dep, _, _ = setup(p, mode, 'explicit')
                (p / 'data/nested').mkdir()
                run(binary, p, 'build', '--all')
                # Exercise saved metadata spelling directly. Hash authority is
                # still keyed by that spelling; preserve its observed value.
                info = p / '.nift/public/a.info.json'
                document = json.loads(info.read_text())
                key = 'data/shared.html'
                document['dependencies'] = [spelling if x == key else x for x in document['dependencies']]
                hashes = document['dependency-hashes']
                if spelling != key and key in hashes:
                    hashes[spelling] = hashes.pop(key)
                info.chmod(0o600)
                info.write_text(json.dumps(document))
                status = run(binary, p, 'status')
                assert 'up to date' in status, (mode, spelling, status)
                dep.write_text('NEW')
                stamp = max(x.stat().st_mtime_ns for x in (p / '.nift/public').glob('*.info.json')) + 1000000
                os.utime(dep, ns=(stamp, stamp))
                status = run(binary, p, 'status')
                assert 'dependency changed:' in status, (mode, spelling, status)
                run(binary, p, 'build', 'a')
                assert 'NEW' in (p / 'public/a.html').read_text()
                run(binary, p, 'build')
                tree = output_tree(p)
                assert all('NEW' in value for value in tree.values())
                # A sidecar is authored input: parent traversal remains rejected,
                # even when lexical normalization would land inside the project.
                (p / 'content/a.deps.json').write_text(json.dumps({'dependencies': ['data/nested/../shared.html']}))
                result = subprocess.run([binary, 'build'], cwd=p, capture_output=True, text=True, timeout=20)
                assert result.returncode != 0, (mode, spelling, result.stdout, result.stderr)
                results.append(tree)
            assert all(tree == results[0] for tree in results)
            rows.append({'mode': mode, 'spelling': spelling, 'saved_alias_clean': True,
                         'changed_targeted_and_remaining': True, 'sidecar_parent_rejected': True,
                         'baseline_matches': len(results) == 2})
if args.output:
    pathlib.Path(args.output).write_text(json.dumps({'passed': True, 'cases': rows}, indent=2) + '\n')
print('Dependency path identity PASS', len(rows))
