#!/usr/bin/env python3
"""BH9 guard: platform / filesystem boundary adversarial.

Guarantee: build output is contained within the output directory. Adversarial
content names (encoded traversal like `..%2f`, parent references, deep nesting,
unicode, spaces) never write outside `public/`; the project tree outside the
output directory is unchanged by a build; and a read-only output directory
yields a controlled failure (non-zero, never a signal or hang).
"""
import argparse, json, os, subprocess, tempfile
from pathlib import Path


def setup(root: Path) -> None:
    (root / '.nift').mkdir()
    (root / 'content').mkdir()
    (root / 'templates').mkdir()
    (root / 'public').mkdir()
    (root / '.nift' / 'config.json').write_text(json.dumps({"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
        "output-ext": ".html", "default-template": "templates/template.html",
        "build-threads": -1, "incremental-mode": "modified"}}))
    (root / 'templates' / 'template.html').write_text('@content\n')


def tracked(root: Path, names: list[str]) -> None:
    (root / '.nift' / 'tracked.json').write_text(json.dumps({"tracked": [
        {"name": n, "title": f"T{n}", "template": "templates/template.html"}
        for n in names]}))


def outside_public_files(root: Path) -> set[str]:
    out = set()
    for p in root.rglob('*'):
        if not p.is_file():
            continue
        rel = p.relative_to(root).as_posix()
        if rel == '.nift/tracked.json' or rel == '.nift/config.json':
            continue
        if rel.startswith('public/') or rel == 'public':
            continue
        out.add(rel)
    return out


ADVERSARIAL_NAMES = [
    '..%2fescape',        # encoded traversal in the name
    'sub/../x',           # parent reference inside the path
    'a b',                # space
    'caf\u00e9',          # unicode
    'deep/one/two/three', # deep nesting
    '..html',             # name that looks like a parent reference
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    args = ap.parse_args()
    nift = str(Path(args.nift).resolve())
    with tempfile.TemporaryDirectory(prefix='nift-bh9.') as td:
        root = Path(td)
        setup(root)
        names = ['/'] + ADVERSARIAL_NAMES
        tracked(root, names)
        (root / 'content' / 'index.html').write_text('<p>i</p>\n')
        for n in ADVERSARIAL_NAMES:
            p = root / 'content' / (n + '.html')
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(f'<p>{n}</p>\n')
        before_outside = outside_public_files(root)

        p = subprocess.run([nift, 'build-all'], cwd=root, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        if p.returncode < 0:
            raise RuntimeError(f"build terminated by signal {-p.returncode}")

        # every output lives under public/, and nothing appeared outside it
        after_outside = outside_public_files(root)
        new_outside = after_outside - before_outside
        if new_outside:
            raise RuntimeError(f"build wrote outside the output directory: {sorted(new_outside)}")
        for f in (root / 'public').rglob('*'):
            if f.is_file() and f.stat().st_size == 0:
                raise RuntimeError(f"empty output {f.relative_to(root)}")

        # a read-only output directory must yield a controlled failure
        (root / 'content' / 'index.html').write_text('<p>i2</p>\n')
        for f in (root / 'public').rglob('*'):
            if f.is_file():
                f.chmod(0o444)
        (root / 'public').chmod(0o555)
        try:
            p2 = subprocess.run([nift, 'build'], cwd=root, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        finally:
            (root / 'public').chmod(0o755)
        if p2.returncode == 0:
            raise RuntimeError("build into a read-only output directory unexpectedly succeeded")
        if p2.returncode < 0:
            raise RuntimeError(f"build into read-only output crashed with signal {-p2.returncode}")

    print('BH9 platform/filesystem boundary adversarial: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
