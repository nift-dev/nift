#!/usr/bin/env python3
"""BH4 guard: incremental state-transition adversarial.

Guarantee: across the modified, hash and hybrid incremental modes, a
deterministic adversarial sequence of state transitions (content change,
`nift rm`, `nift mv`, `nift track`, template change, tracked metadata change)
must leave the output set exact — every expected page present and non-empty,
no stale/orphan outputs — and incremental output must equal a clean rebuild.

A build that drops a page, leaves a stale output, or fails to propagate a
template change is caught here even when the remaining outputs are
self-consistent.
"""
import argparse, hashlib, json, shutil, subprocess, tempfile
from pathlib import Path


def run(nift, cwd, *args, expect=0):
    proc = subprocess.run([nift, *args], cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    if proc.returncode != expect:
        raise RuntimeError(f"{' '.join(args)} returned {proc.returncode}:\n{proc.stdout}")
    return proc.stdout


def public_set(project: Path) -> set[str]:
    pub = project / 'public'
    if not pub.exists():
        return set()
    return {p.relative_to(pub).as_posix() for p in pub.rglob('*') if p.is_file()}


def public_hash(project: Path) -> str:
    h = hashlib.sha256()
    for rel in sorted(public_set(project)):
        h.update(rel.encode())
        h.update((project / 'public' / rel).read_bytes())
    return h.hexdigest()


def assert_outputs(project: Path, expected: set[str], label: str) -> None:
    actual = public_set(project)
    missing = set(expected) - actual
    stale = actual - set(expected)
    if missing or stale:
        raise RuntimeError(f"{label}: output-set mismatch\n"
                           f"missing={sorted(missing)}\nstale={sorted(stale)}")
    for rel in sorted(expected):
        p = project / 'public' / rel
        if p.stat().st_size == 0:
            raise RuntimeError(f"{label}: output {rel!r} is empty")


def setup(project: Path, mode: str, names: list[str]) -> None:
    (project / '.nift').mkdir(parents=True)
    (project / 'content').mkdir()
    (project / 'templates').mkdir()
    (project / 'public').mkdir()
    (project / '.nift' / 'config.json').write_text(json.dumps({"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
        "output-ext": ".html", "default-template": "templates/template.html",
        "build-threads": -1, "incremental-mode": mode,
    }}))
    (project / '.nift' / 'tracked.json').write_text(json.dumps({"tracked": [
        {"name": n, "title": n.upper(), "template": "templates/template.html"}
        for n in names]}))
    (project / 'templates' / 'template.html').write_text('@content\n')
    for n in names:
        p = project / 'content' / (n + '.html')
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(f'<p>{n}</p>\n')


def transition(nift: str, proj: Path, mode: str, expected: set[str], label: str) -> None:
    run(nift, proj, 'build')
    assert_outputs(proj, expected, f'{mode}:{label}')
    # incremental output must byte-match a clean rebuild of this exact state,
    # not merely share the same output set; stale content from an earlier
    # transition cannot hide behind a later one.
    fresh = proj.parent / f'{proj.name}-oracle'
    if fresh.exists():
        shutil.rmtree(fresh)
    shutil.copytree(proj, fresh)
    shutil.rmtree(fresh / 'public')
    (fresh / 'public').mkdir()
    run(nift, fresh, 'build-all')
    if public_hash(fresh) != public_hash(proj):
        raise RuntimeError(f"{mode}:{label}: incremental/clean bytes differ after transition")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    args = ap.parse_args()
    nift = str(Path(args.nift).resolve())
    with tempfile.TemporaryDirectory(prefix='nift-bh4.') as td:
        base = Path(td)
        for mode in ('modified', 'hash', 'hybrid'):
            proj = base / mode
            proj.mkdir()
            setup(proj, mode, ['a', 'b', 'c'])
            expected = {'a.html', 'b.html', 'c.html'}
            run(nift, proj, 'build-all')
            assert_outputs(proj, expected, f'{mode}:baseline')

            # content change
            (proj / 'content' / 'b.html').write_text('<p>B2</p>\n')
            transition(nift, proj, mode, expected, 'content-change')
            if '<p>B2</p>' not in (proj / 'public' / 'b.html').read_text():
                raise RuntimeError(f"{mode}:content-change: b.html does not reflect changed content")

            # remove b via the tracked command; stale output must disappear
            run(nift, proj, 'rm', 'b')
            expected.discard('b.html')
            transition(nift, proj, mode, expected, 'rm-b')

            # rename c -> d via the tracked command; old output must go
            run(nift, proj, 'mv', 'c', 'd')
            expected.discard('c.html')
            expected.add('d.html')
            transition(nift, proj, mode, expected, 'mv-c-d')

            # add e via the tracked command
            (proj / 'content' / 'e.html').write_text('<p>e</p>\n')
            run(nift, proj, 'track', 'e', 'E', 'templates/template.html')
            expected.add('e.html')
            transition(nift, proj, mode, expected, 'track-e')
            if '<p>e</p>' not in (proj / 'public' / 'e.html').read_text():
                raise RuntimeError(f"{mode}:track-e: e.html does not reflect tracked content")

            # template change must propagate to every output
            (proj / 'templates' / 'template.html').write_text('T2:@content\n')
            transition(nift, proj, mode, expected, 'template-change')
            for rel in sorted(expected):
                if 'T2:' not in (proj / 'public' / rel).read_text():
                    raise RuntimeError(f"{mode}:template: {rel!r} not rebuilt")

            # tracked metadata change must not corrupt the output set
            tr = json.loads((proj / '.nift' / 'tracked.json').read_text())
            tr['tracked'][0]['title'] = 'Renamed'
            (proj / '.nift' / 'tracked.json').write_text(json.dumps(tr))
            transition(nift, proj, mode, expected, 'metadata-change')

            # incremental output must equal a clean rebuild of the same project
            fresh = base / f'{mode}-fresh'
            shutil.copytree(proj, fresh)
            shutil.rmtree(fresh / 'public')
            (fresh / 'public').mkdir()
            run(nift, fresh, 'build-all')
            if public_set(fresh) != public_set(proj) or public_hash(fresh) != public_hash(proj):
                raise RuntimeError(f"{mode}:equivalence: incremental/clean mismatch")
            print(f'{mode}: PASS')

    print('BH4 incremental state-transition adversarial: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
