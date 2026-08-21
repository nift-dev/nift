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
        run(nift, root, 'init')

        # scaffold present
        for rel in ('.nift/config.json', '.nift/tracked.json',
                    'templates/template.html', 'templates/head.html',
                    'content/index.html', 'public/index.html'):
            if not (root / rel).is_file():
                raise RuntimeError(f"init scaffold missing {rel!r}")

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

        # a clean rebuild must reproduce the init'd output exactly
        shutil.rmtree(root / 'public')
        shutil.rmtree(root / '.nift' / 'public')
        run(nift, root, 'build-all')
        if tree_hash(root / 'public') != init_pub:
            raise RuntimeError("clean rebuild does not reproduce init'd public output")
        if tree_hash(root / '.nift' / 'public') != init_meta:
            raise RuntimeError("clean rebuild does not reproduce init'd page metadata")

        # builds must be idempotent
        run(nift, root, 'build')
        if tree_hash(root / 'public') != init_pub:
            raise RuntimeError("incremental build is not idempotent")

    print('BH6 init/starter functional truth: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
