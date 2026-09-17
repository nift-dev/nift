#!/usr/bin/env python3
"""Full-build typed-content scaling gate.

Detects a return to quadratic full-build behaviour without a brittle absolute
time threshold. Builds 250 and 1000 plain typed-content pages (front-matter
content with a tracked content type and an @content template — no project
reference, which exercises the base render path where the quadratic behaviour
appeared) and compares the ratio of the two build times.

A near-linear build costs ~4x for 4x pages; the historical quadratic path
measured ~16x. The gate fails above an 8x ratio (a 2x margin over the linear
expectation) so slower CI machines do not cause spurious failures.

Run: NIFT_BIN=./nift python3 benchmarks/typed_content_scaling.py
"""
import json, os, pathlib, subprocess, sys, tempfile, time

BIN = pathlib.Path(os.environ.get('NIFT_BIN', './nift')).resolve()


def build(n):
    with tempfile.TemporaryDirectory() as td:
        r = pathlib.Path(td)
        for d in ('.nift', 'content', 'content/posts', 'templates', 'public'):
            (r / d).mkdir(parents=True, exist_ok=True)
        (r / '.nift/config.json').write_text(json.dumps({
            'config': {'content-dir': 'content/', 'content-ext': '.md', 'output-dir': 'public/',
                       'output-ext': '.html', 'default-template': 'templates/template.html',
                       'build-threads': -1, 'incremental-mode': 'modified'}}))
        (r / 'templates/template.html').write_text('<!doctype html><body>@content</body>')
        tracked = [{'name': '/', 'title': 'Home', 'template': 'templates/template.html', 'type': 'post'}]
        (r / 'content/index.md').write_text('Home\n')
        for i in range(n):
            tracked.append({'name': f'posts/p{i}', 'title': f'P{i}',
                            'template': 'templates/template.html', 'type': 'post'})
            (r / f'content/posts/p{i}.md').write_text(f'Body {i}\n')
        (r / '.nift/tracked.json').write_text(json.dumps({'tracked': tracked}))
        t0 = time.perf_counter()
        p = subprocess.run([str(BIN), 'build', '--all'], cwd=r, capture_output=True, text=True)
        elapsed = time.perf_counter() - t0
        if p.returncode:
            raise SystemExit(f'build failed at {n}: {p.stderr}')
        return elapsed


def main():
    t250 = build(250)
    t1000 = build(1000)
    ratio = t1000 / t250 if t250 > 0 else 0
    print(f'250 pages: {t250:.3f}s  1000 pages: {t1000:.3f}s  ratio={ratio:.1f}x')
    if ratio > 8.0:
        raise SystemExit(f'FAIL: full-build scaling is superlinear (ratio {ratio:.1f}x for 4x pages)')
    print('PASS: full-build typed-content scaling approximately linear')


if __name__ == '__main__':
    main()