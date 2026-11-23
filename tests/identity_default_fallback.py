#!/usr/bin/env python3
"""Ordinary builds use dependency metadata with or without an optional catalogue."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from shared_dependency_targeted_reproducer import setup

parser = argparse.ArgumentParser()
parser.add_argument('--nift', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
binary = args.nift.resolve()

with tempfile.TemporaryDirectory(prefix='nift-identity-default-') as directory:
    project = Path(directory).resolve()
    dependency, target, changed = setup(project, 'modified', 'explicit')
    manifest = project / '.nift/identity-stage1/manifest.bin'

    def run_candidate(*args):
        result = subprocess.run([binary, *args], cwd=project, capture_output=True, text=True, timeout=45)
        assert result.returncode == 0, (args, result.stdout, result.stderr)

    run_candidate('build', '--all')
    assert 'OLD' in (project / 'public/a.html').read_text()
    assert not manifest.parent.exists(), 'ordinary builds must not publish a catalogue'

    # A corrupt optional catalogue must leave the normal dependency fallback live.
    manifest.parent.mkdir()
    corrupt = b'invalid optional catalogue'
    manifest.write_bytes(corrupt)
    dependency.write_text(changed)
    # Set an unambiguous newer timestamp even on coarse filesystems.
    future = max(path.stat().st_mtime for path in (project / '.nift/public').glob('*.info.json')) + 2
    os.utime(dependency, (future, future))
    run_candidate('build', target)
    assert changed in (project / 'public/a.html').read_text()
    assert manifest.read_bytes() == corrupt, 'fallback must leave the catalogue untouched'
    args.output.write_text(json.dumps({'passed': True, 'cases': [
        'ordinary build does not publish', 'corrupt catalogue leaves dependency fallback live',
    ]}, indent=2))

