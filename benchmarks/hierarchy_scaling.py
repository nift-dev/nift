#!/usr/bin/env python3
"""Hierarchy index construction scaling guard.

Detects hierarchy-index construction drifting toward O(N^2). Builds a nested
hierarchy at N and 4N pages and compares the time to build the index (a fresh
process evaluating one page(name) query, which triggers lazy construction).
Expected construction is ~O(N), so 4x pages should cost ~4x (a generous 7x
budget); an accidental quadratic construction would measure ~16x.

Run: NIFT_BIN=./nift python3 benchmarks/hierarchy_scaling.py
"""
import json, os, pathlib, subprocess, sys, tempfile, time

BIN = pathlib.Path(os.environ.get('NIFT_BIN', './nift')).resolve()


def fixture(root, n):
    r = pathlib.Path(root)
    for d in ('.nift', 'content', 'templates', 'public'):
        (r / d).mkdir(parents=True, exist_ok=True)
    (r / '.nift/config.json').write_text(json.dumps({
        'config': {'content-dir': 'content/', 'content-ext': '.md', 'output-dir': 'public/',
                   'output-ext': '.html', 'default-template': 'templates/template.html',
                   'build-threads': -1, 'incremental-mode': 'modified'}}))
    (r / 'templates/template.html').write_text('@content')
    tracked = [{'name': '/', 'title': 'Home', 'template': 'templates/template.html'}]
    (r / 'content/index.md').write_text('# Home\n')
    for i in range(n):
        name = f"s{i % 20}/sub{i % 20}/p{i}"
        tracked.append({'name': name, 'title': f'T{i}', 'template': 'templates/template.html'})
        p = r / 'content' / pathlib.Path(name + '.md')
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(f'# {name}\n')
    (r / '.nift/tracked.json').write_text(json.dumps({'tracked': tracked}))


def construct_seconds(td):
    walls = []
    for _ in range(3):
        t0 = time.perf_counter()
        p = subprocess.run([str(BIN), 'eval', 'page("s0/sub0/p0").parent.name'], cwd=td,
                           capture_output=True, text=True)
        walls.append(time.perf_counter() - t0)
        if p.returncode != 0:
            raise SystemExit(f'eval failed: {p.stderr}')
    return min(walls)


def main():
    sizes = []
    for n in (500, 2000):
        td = pathlib.Path(tempfile.mkdtemp(prefix='nifthier'))
        fixture(td, n)
        sizes.append((n, construct_seconds(td)))
        subprocess.run(['rm', '-rf', str(td)])
    n1, t1 = sizes[0]
    n2, t2 = sizes[1]
    ratio = t2 / t1 if t1 > 0 else 0
    expected = n2 / n1
    print(f'{n1} pages: {t1:.3f}s  {n2} pages: {t2:.3f}s  ratio={ratio:.1f}x  (4x pages -> {ratio/4:.1f}x per 4x)')
    if ratio > expected * 2.0:  # allow generous constant/startup factor, reject quadratic (~4x per 4x -> 16x total)
        raise SystemExit(f'FAIL: hierarchy construction is superlinear (ratio {ratio:.1f}x for {expected:.0f}x pages)')
    print('PASS: hierarchy index construction approximately O(N)')


if __name__ == '__main__':
    main()