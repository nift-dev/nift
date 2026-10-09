#!/usr/bin/env python3
"""Black-box transformation contracts: ownership, parity rails and mode isolation."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
MODES = ('migration', 'rewrite', 'redesign')
parser = argparse.ArgumentParser()
parser.add_argument('--mode', choices=('rewrite', 'redesign'), required=True)
parser.add_argument('--nift')
args = parser.parse_args()
mode = args.mode
binary = str(Path(args.nift or os.environ.get('NIFT_BIN', ROOT / 'nift')).resolve())
checks = 0

def snapshot(root):
    return {str(p.relative_to(root)): p.read_bytes() for p in root.rglob('*') if p.is_file()}

def run(root, *flags, ok=True):
    global checks
    before = snapshot(root)
    result = subprocess.run([binary, 'init', *flags], cwd=root, capture_output=True)
    assert (result.returncode == 0) == ok, result.stderr.decode(errors='replace')
    if not ok:
        assert snapshot(root) == before, 'failed preflight mutated files'
    checks += 1
    return result

with tempfile.TemporaryDirectory(prefix='nift-transformation-') as tmp:
    base = Path(tmp)
    def project(name, files=None):
        root = base / name
        root.mkdir()
        for name, content in (files or {}).items():
            p = root / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(content)
        return root
    workbook = mode.upper() + '.md'
    fresh = project('fresh')
    run(fresh, '--' + mode)
    for name in (workbook, 'HANDOVER.md'):
        assert (fresh / name).read_bytes() == (ROOT / 'tests/fixtures' / name).read_bytes()
    names = ('REFERENCE.md', 'BEHAVIOR-CONTRACT.md', 'DESIGN-CONTRACT.md') if mode == 'rewrite' else ('REQUIREMENTS.md', 'DESIGN-BRIEF.md', 'ROUTE-MAP.md')
    for name in ('STATUS.md', 'README.md', 'CONTENT-INVENTORY.md', 'EXTERNAL-INPUTS.md', 'KNOWN-DIVERGENCES.md', *names):
        assert (fresh / 'investigation' / name).is_file(), name
    agents = (fresh / 'AGENTS.md').read_text()
    assert agents.count(f'<!-- nift:{mode}:start -->') == 1
    assert agents.count(f'<!-- nift:{mode}:end -->') == 1
    assert 'EXPERIMENTAL' in agents and 'Stop and report' in agents
    status = (fresh / 'investigation/STATUS.md').read_text()
    labels = ('Validate design/behaviour parity', 'Performance campaign', 'Full parity revalidation', 'Final benchmarks') if mode == 'rewrite' else ('Accessibility/browser validation', 'Performance campaign', 'Final contract revalidation', 'Final meaningful benchmark/comparison')
    positions = [status.index(label) for label in labels]
    assert positions == sorted(positions)
    method = (fresh / workbook).read_text()
    for text in ('React', 'Vue', 'Svelte', 'Solid', 'Web Components', 'vanilla JavaScript', 'authored', 'rendered', 'hybrid', 'EXPERIMENTAL'):
        assert text in method, text
    for p in fresh.rglob('*.md'):
        data = p.read_bytes()
        assert data.endswith(b'\n') and b'\r' not in data, p
        assert all(line.rstrip(b' \t') == line for line in data.splitlines()), p
    second = project('second')
    run(second, '--' + mode)
    for name, data in snapshot(fresh).items():
        if name.endswith('.md'):
            assert snapshot(second)[name] == data, name
    original = snapshot(fresh)
    run(fresh, '--' + mode, f'--{mode}-existing=replace')
    run(fresh, '--' + mode)
    assert snapshot(fresh) == original, 'existing project rerun changed a file'
    for policy in ('error', 'keep', 'append', 'replace'):
        root = project(policy, {workbook: 'owned by user\n', 'HANDOVER.md': 'state\n', 'README.md': '# custom\n', 'AGENTS.md': '# owner instructions\n'})
        run(root, '--' + mode, f'--{mode}-existing={policy}', ok=policy != 'error')
        if policy == 'error':
            continue
        assert (root / 'AGENTS.md').read_text().startswith('# owner instructions\n')
        if policy == 'keep':
            assert (root / workbook).read_text() == 'owned by user\n'
            assert (root / 'README.md').read_text() == '# custom\n'
        elif policy == 'replace':
            assert (root / workbook).read_bytes() == (ROOT / 'tests/fixtures' / workbook).read_bytes()
        else:
            assert (root / workbook).read_text().startswith('owned by user\n')
            # Remove only project state to exercise append idempotence.
            (root / '.nift/config.json').unlink()
            (root / '.nift/tracked.json').unlink()
            before = {k:v for k,v in snapshot(root).items() if k.endswith('.md')}
            run(root, '--' + mode, f'--{mode}-existing=append')
            assert {k:v for k,v in snapshot(root).items() if k.endswith('.md')} == before
    root = project('unrelated', {'README.md':'# custom\n', 'investigation/STATUS.md':'existing ledger\n', 'AGENTS.md':'owner\n'})
    run(root, '--' + mode)
    assert (root / 'README.md').read_text() == '# custom\n'
    assert (root / 'investigation/STATUS.md').read_text() == 'existing ledger\n'
    for other in MODES:
        if other == mode:
            continue
        for policy in ('error','keep','append','replace'):
            for kind in ('workbook','agents'):
                files = {other.upper()+'.md':'foreign contract\n'} if kind == 'workbook' else {'AGENTS.md': f'<!-- nift:{other}:start -->\n<!-- nift:{other}:end -->\n'}
                root = project(f'cross-{other}-{policy}-{kind}', files)
                run(root, '--'+mode, f'--{mode}-existing={policy}', ok=False)
        root = project('flags-'+other)
        run(root, '--'+mode, '--'+other, ok=False)
        root = project('policy-'+other)
        run(root, '--'+mode, f'--{other}-existing=keep', ok=False)
    for policy in ('error','keep','append','replace'):
        for kind, file, marker in (('agents','AGENTS.md',mode), ('workbook',workbook,mode+'-template'), ('handover','HANDOVER.md','handover-template'), ('status','investigation/STATUS.md',mode+'-status')):
            for defect, text in (('start', f'<!-- nift:{marker}:start -->\n'), ('reverse',f'<!-- nift:{marker}:end -->\n<!-- nift:{marker}:start -->\n'), ('multiple',f'<!-- nift:{marker}:start -->\n'*2)):
                root = project(f'malformed-{policy}-{kind}-{defect}', {file:text})
                run(root, '--'+mode, f'--{mode}-existing={policy}', ok=False)
    # Migration must reject the new modes' contracts too, under every policy.
    for foreign_mode in ('rewrite', 'redesign'):
        for policy in ('error', 'keep', 'append', 'replace'):
            root = project(f'migration-cross-{foreign_mode}-{policy}', {foreign_mode.upper()+'.md': 'foreign contract\n'})
            run(root, '--migration', f'--migration-existing={policy}', ok=False)
    for flags in ((f'--{mode}-existing=keep',), ('--'+mode,f'--{mode}-existing=unknown'), ('--'+mode,f'--{mode}-existing'), ('--'+mode,'--target=vercel','--ext=.txt')):
        root = project('invalid-'+str(checks))
        run(root,*flags,ok=False)
    for flags in (('--'+mode,'--target=vercel','--ext=.htm'), ('--'+mode,'--ext=.txt'), ('--'+mode,'--handover')):
        root = project('combined-'+str(checks))
        run(root,*flags)
        assert (root/workbook).exists()
    for flags in ((),('--handover',),('--migration-existing=keep',)):
        root = project('plain-'+str(checks))
        run(root,*flags)
        assert not (root/'AGENTS.md').exists()
        assert not any((root/(m.upper()+'.md')).exists() for m in MODES)
        assert (root/'HANDOVER.md').exists() == ('--handover' in flags)
print(f'init-{mode}: PASS ({checks} black-box initialization cases)')
