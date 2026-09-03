#!/usr/bin/env python3
"""BH6 guard: init / starter functional truth.

Guarantee: `nift init` scaffolding is functionally true. The generated project
builds out of the box; the config/tracked/template/content state is internally
consistent (every @input reference resolves, content-ext/output-ext agree with
the actual outputs); a clean rebuild reproduces the init'd public output and
page metadata exactly; and builds are idempotent.

A scaffold whose init output differs from a clean rebuild, whose build is not
idempotent, whose template references a missing fragment, or whose config lies
about the output extensions is caught here.
"""
import argparse, hashlib, json, shutil, subprocess, tempfile
from pathlib import Path


def tree_hash(root: Path) -> dict[str, str]:
    if not root.exists():
        return {}
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(x for x in root.rglob('*') if x.is_file())}


def run(nift, cwd, *args):
    p = subprocess.run([nift, *args], cwd=cwd, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{' '.join(args)} returned {p.returncode}:\n{p.stdout}")
    return p


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    args = ap.parse_args()
    nift = str(Path(args.nift).resolve())
    with tempfile.TemporaryDirectory(prefix='nift-bh6.') as td:
        root = Path(td)
        init_result = run(nift, root, 'init')
        if '1 file built successfully' not in init_result.stdout:
            raise RuntimeError(f"init build summary does not use file terminology:\n{init_result.stdout}")

        # Pages are tracked, while starter CSS/JS are ordinary files maintained
        # directly in the public tree.
        for rel in ('.nift/config.json', '.nift/tracked.json',
                    'templates/template.html', 'templates/head.html',
                    'content/index.html', 'public/index.html',
                    'public/assets/css/style.css', 'public/assets/js/script.js'):
            if not (root / rel).is_file():
                raise RuntimeError(f"init scaffold missing {rel!r}")
        tracked = json.loads((root / '.nift' / 'tracked.json').read_text())['tracked']
        tracked_names = {e['name'] for e in tracked}
        if tracked_names.intersection({'assets/css/style', 'assets/js/script'}):
            raise RuntimeError(f"init scaffold unexpectedly tracks static assets: {tracked_names!r}")
        if (root / 'content' / 'assets').exists():
            raise RuntimeError("init scaffold unexpectedly created content/assets")

        # template references resolve
        tpl = (root / 'templates' / 'template.html').read_text()
        for ref in __import__('re').findall(r'@input\(["\']([^"\']+)["\']\)', tpl):
            if not (root / ref).is_file():
                raise RuntimeError(f"template references missing @input {ref!r}")

        # config/tracked consistency: content-ext -> output-ext mapping
        cfg = json.loads((root / '.nift' / 'config.json').read_text())['config']
        content_ext = cfg.get('content-ext', '.html')
        output_ext = cfg.get('output-ext', '.html')
        tracked = json.loads((root / '.nift' / 'tracked.json').read_text())['tracked']
        for ent in tracked:
            rel = 'index' if ent['name'] == '/' else ent['name']
            cext = ent.get('content-ext', content_ext)
            oext = ent.get('output-ext', output_ext)
            src = root / 'content' / (rel + cext)
            if not src.is_file():
                raise RuntimeError(f"tracked entry {ent['name']!r} has no source {src.name}")
            out = root / 'public' / (rel + oext)
            if not out.is_file():
                raise RuntimeError(f"tracked entry {ent['name']!r} produced no output {out.name}")

        init_pub = tree_hash(root / 'public')
        init_meta = tree_hash(root / '.nift' / 'public')
        if not init_pub:
            raise RuntimeError("init produced no public output")

        # A clean page rebuild must leave directly maintained assets untouched.
        (root / 'public' / 'index.html').unlink()
        shutil.rmtree(root / '.nift' / 'public')
        run(nift, root, 'build', '--all')
        if tree_hash(root / 'public') != init_pub:
            raise RuntimeError("clean rebuild does not reproduce init'd public output")
        if tree_hash(root / '.nift' / 'public') != init_meta:
            raise RuntimeError("clean rebuild does not reproduce init'd page metadata")

        # builds must be idempotent
        up_to_date_build = run(nift, root, 'build')
        if tree_hash(root / 'public') != init_pub:
            raise RuntimeError("incremental build is not idempotent")
        if '1 tracked file is up to date' not in up_to_date_build.stdout:
            raise RuntimeError(f"up-to-date build summary does not use tracked-file terminology:\n{up_to_date_build.stdout}")

        status = run(nift, root, 'status')
        if 'all 1 tracked file is up to date' not in status.stdout:
            raise RuntimeError(f"status summary does not use tracked-file terminology:\n{status.stdout}")

    print('BH6 init/starter functional truth: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
