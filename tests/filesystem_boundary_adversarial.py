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


def outside_public_files(root: Path) -> dict[str, str]:
    """Every file outside public/ (excluding the two metadata files), keyed by
    relative path with its content hash — so a tool that corrupts an existing
    source file is caught, not just one that writes a new file."""
    import hashlib
    out = {}
    for p in root.rglob('*'):
        if not p.is_file():
            continue
        rel = p.relative_to(root).as_posix()
        if rel.startswith('.nift/'):
            continue  # nift's own project-state directory is not build output
        if rel.startswith('public/') or rel == 'public':
            continue
        out[rel] = hashlib.sha256(p.read_bytes()).hexdigest()
    return out


# Names Nift builds and must contain within the output directory.
ADVERSARIAL_NAMES = [
    '..%2fescape',        # encoded traversal in the name
    'a b',                # space
    'caf\u00e9',          # unicode
    'deep/one/two/three', # deep nesting
    '..html',             # name that looks like a parent reference
]

# A name Nift must REJECT (controlled) rather than follow outside the tree.
REJECTED_NAME = 'sub/../x'  # parent reference inside the path


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

        # the valid page must actually have been built: a tool that "builds"
        # nothing (or exits 1 without writing) is not a green run
        index_out = root / 'public' / 'index.html'
        if not index_out.is_file() or index_out.stat().st_size == 0:
            raise RuntimeError("build produced no output for the valid page")

        # every output lives under public/, and nothing appeared outside it;
        # outside-tree comparison is by content hash, so corrupting an existing
        # source file is caught
        after_outside = outside_public_files(root)
        if after_outside != before_outside:
            changed = []
            for rel in sorted(set(before_outside) | set(after_outside)):
                if before_outside.get(rel) != after_outside.get(rel):
                    changed.append(rel)
            raise RuntimeError(f"build wrote or modified files outside the output directory: {changed}")
        for f in (root / 'public').rglob('*'):
            if f.is_file() and f.stat().st_size == 0:
                raise RuntimeError(f"empty output {f.relative_to(root)}")

        # a parent-reference name must be REJECTED with a controlled failure,
        # never followed outside the output directory or crashed on
        rejected = root / 'content' / (REJECTED_NAME + '.html')
        rejected.parent.mkdir(parents=True, exist_ok=True)
        rejected.write_text('<p>evil</p>\n')
        tracked(root, ['/'] + ADVERSARIAL_NAMES + [REJECTED_NAME])
        before_rej = outside_public_files(root)
        pr = subprocess.run([nift, 'build-all'], cwd=root, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        if pr.returncode == 0:
            raise RuntimeError(f"parent-reference name {REJECTED_NAME!r} was accepted")
        if pr.returncode < 0:
            raise RuntimeError(f"parent-reference name crashed with signal {-pr.returncode}")
        if outside_public_files(root) != before_rej:
            raise RuntimeError("rejected-name build wrote outside the output directory")

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
