"""Native sidecar identity and imported-platform certificate contracts."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from shared_dependency_targeted_reproducer import setup
from consumer_dependency_snapshots import hash_bytes

parser = argparse.ArgumentParser()
parser.add_argument('--nift', required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--export', required=True)
parser.add_argument('--imports', type=Path)
args = parser.parse_args()
binary = str(Path(args.nift).resolve())
rows = []
exported = {}
platform = 'windows-lexical-v1' if os.name == 'nt' else 'posix-lexical-v1'


def run(project, *commands):
    return subprocess.run([binary, *commands], cwd=project, capture_output=True, text=True)


def build(project, *commands):
    result = run(project, 'build', *commands)
    assert result.returncode == 0, (result.stdout, result.stderr)


def fixture(project, mode, raw):
    project.mkdir()
    setup(project, mode, 'explicit')
    (project / 'data/one.txt').write_text('one')
    (project / 'data/two.txt').write_text('two')
    side = project / 'content/a.deps.json'
    side.write_bytes(raw)
    for path in [side, *list((project / 'data').iterdir())]:
        os.utime(path, ns=(1600000000000000000,) * 2)
    build(project, '--all')
    return side, project / '.nift/public/a.info.json'


with tempfile.TemporaryDirectory(prefix='sidecar-platform-') as temporary:
    root = Path(temporary)
    for mode in ['modified', 'hash', 'hybrid']:
        raw = b'{"dependencies":["data/one.txt"]}'
        project = root / ('export-' + mode)
        side, meta = fixture(project, mode, raw)
        info = json.loads(meta.read_text())
        state = info['sidecar-state']
        assert state['platform'] == platform
        assert bytes.fromhex(state['bytes-hex']) == raw
        assert [info['dependencies'][i] for i in state['declaration-indices']] == ['data/one.txt']
        exported[mode] = info
        rows.append({'mode': mode, 'case': 'native-export', 'passed': True})

        # Preserve the actual timestamp supported by this native filesystem.
        stamp = side.stat().st_mtime_ns
        side.write_bytes(b'{"dependencies":["data/two.txt"]}')
        os.utime(side, ns=(stamp, stamp))
        assert side.stat().st_mtime_ns == stamp
        build(project)
        (project / 'data/two.txt').unlink()
        result = run(project, 'build')
        assert result.returncode != 0, (mode, result.stdout, result.stderr)
        rows.append({'mode': mode, 'case': 'preserved-mtime-replaced-declaration', 'passed': True})

    # Native Windows accepts separator spelling through its own lexical policy.
    if os.name == 'nt':
        project = root / 'windows-separators'
        raw = b'{"dependencies":["data\\\\one.txt"]}'
        side, meta = fixture(project, 'modified', raw)
        info = json.loads(meta.read_text())
        assert bytes.fromhex(info['sidecar-state']['bytes-hex']) == raw
        assert [info['dependencies'][i] for i in info['sidecar-state']['declaration-indices']] == ['data/one.txt']
        build(project)
        rows.append({'case': 'windows-separator-normalization', 'passed': True})

    if args.imports:
        foreign_files = list(args.imports.rglob('sidecar-export.json'))
        assert foreign_files, args.imports
        foreign_count = 0
        for number, source in enumerate(foreign_files):
            imported = json.loads(source.read_text())
            if imported['platform'] == platform:
                continue
            foreign_count += 1
            for mode, foreign_info in imported['metadata'].items():
                project = root / f'import-{number}-{mode}'
                raw = b'{"dependencies":["data/one.txt"]}'
                side, meta = fixture(project, mode, raw)
                # Copy a real certificate generated on the other platform.
                # Fixture-relative dependencies have the same identities here.
                local = json.loads(meta.read_text())
                assert local['dependencies'] == foreign_info['dependencies']
                local['sidecar-state'] = foreign_info['sidecar-state']
                meta.chmod(0o644)
                meta.write_text(json.dumps(local))
                build(project)
                # Make cached reuse observable: a matching byte certificate
                # with foreign provenance must not bypass the JSON validator.
                side.write_bytes(b'{broken')
                stamp = meta.stat().st_mtime_ns - 2000000000
                os.utime(side, ns=(stamp, stamp))
                local['sidecar-state']['bytes-hex'] = b'{broken'.hex()
                if mode != 'modified':
                    local['dependency-hashes']['content/a.deps.json'] = hash_bytes(b'{broken')
                meta.chmod(0o644)
                meta.write_text(json.dumps(local))
                result = run(project, 'build')
                assert result.returncode != 0 and 'invalid user dependencies JSON' in result.stdout + result.stderr, (source, mode, result.stdout, result.stderr)
                rows.append({'mode': mode, 'case': 'foreign-provenance-fallback', 'source_platform': imported['platform'], 'passed': True})
        assert foreign_count > 0, 'No incompatible platform export supplied'

Path(args.export).write_text(json.dumps({'platform': platform, 'metadata': exported}, indent=2) + '\n')
Path(args.output).write_text(json.dumps({'passed': True, 'platform': platform, 'cases': rows}, indent=2) + '\n')
print('Native platform contracts PASS', len(rows))
