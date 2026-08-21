#!/usr/bin/env python3
"""BH8 guard: performance / complexity invariants.

Guarantee: incremental builds are change-proportional. A no-op build rewrites
no output, and changing one source rebuilds exactly that page (no full
re-render). A tool that rewrites everything on every build, or rebuilds the
whole site for a one-page change, is caught here.

Measured structurally (mtime + content), not by wall clock, so the invariant
holds regardless of machine speed.
"""
import argparse, json, subprocess, tempfile, time
from pathlib import Path


def setup(root: Path, mode: str, n: int = 30) -> None:
    (root / '.nift').mkdir()
    (root / 'content').mkdir()
    (root / 'templates').mkdir()
    (root / 'public').mkdir()
    (root / '.nift' / 'config.json').write_text(json.dumps({"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
        "output-ext": ".html", "default-template": "templates/template.html",
        "build-threads": -1, "incremental-mode": mode}}))
    (root / '.nift' / 'tracked.json').write_text(json.dumps({"tracked": [
        {"name": f"p{i}", "title": f"P{i}", "template": "templates/template.html"}
        for i in range(n)]}))
    (root / 'templates' / 'template.html').write_text('@content\n')
    for i in range(n):
        (root / 'content' / f'p{i}.html').write_text(f'<p>{i}</p>\n')


def run(nift: str, root: Path, *args) -> None:
    subprocess.run([nift, *args], cwd=root, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)


def stamps(root: Path) -> dict[str, tuple[int, int, bytes]]:
    return {p.name: (p.stat().st_mtime_ns, p.stat().st_size, p.read_bytes())
            for p in (root / 'public').glob('*.html')}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    args = ap.parse_args()
    nift = str(Path(args.nift).resolve())
    with tempfile.TemporaryDirectory(prefix='nift-bh8.') as td:
        base = Path(td)
        for mode in ('modified', 'hash', 'hybrid'):
            root = base / mode
            root.mkdir()
            setup(root, mode)
            run(nift, root, 'build-all')
            before = stamps(root)

            # a no-op build must not rewrite any output
            time.sleep(0.02)
            run(nift, root, 'build')
            after = stamps(root)
            rewritten = [k for k in before if before[k] != after.get(k)]
            if rewritten:
                raise RuntimeError(f"{mode}:noop: no-op build rewrote outputs {rewritten}")

            # a one-page change must rebuild exactly that page: p7's output
            # BYTES and mtime must change and reflect the new source content,
            # and every OTHER output must be completely untouched (mtime AND
            # bytes) - a full re-render that rewrites unrelated pages is RED
            # even when their bytes are identical
            time.sleep(0.02)
            (root / 'content' / 'p7.html').write_text('<p>7-CHANGED</p>\n')
            run(nift, root, 'build')
            after2 = stamps(root)
            if b'7-CHANGED' not in after2['p7.html'][2]:
                raise RuntimeError(f"{mode}:proportional: p7.html output does not reflect the source change")
            if before['p7.html'][2] == after2['p7.html'][2]:
                raise RuntimeError(f"{mode}:proportional: p7.html bytes did not change on rebuild")
            if before['p7.html'][0] == after2['p7.html'][0]:
                raise RuntimeError(f"{mode}:proportional: p7.html mtime did not change on rebuild")
            touched_others = [k for k in before if k != 'p7.html' and before[k] != after2.get(k)]
            if touched_others:
                raise RuntimeError(
                    f"{mode}:proportional: unrelated pages were rewritten {touched_others}")

            print(f'{mode}: PASS')

    print('BH8 performance/complexity invariants: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
