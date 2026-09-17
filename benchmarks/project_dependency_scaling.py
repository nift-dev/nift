#!/usr/bin/env python3
"""Project-using build dependency-scaling gate.

Protects the second discovered performance defect: a page that references
project.* must not register every tracked file as a dependency. The old
behaviour produced O(n) dependency records per project page and therefore
O(n^2) dependency bookkeeping across a project-wide build. The correction
records a single compact project fingerprint instead.

The gate asserts two machine-independent invariants:

1. Dependency bookkeeping stays O(1) per project-using page. After building a
   project where every page references project.*, no page-info dependency list
   may grow with the tracked-file count (a fixed budget is enforced).
2. Full-build scaling stays approximately linear. Building 4x pages must not
   cost ~16x (the historical quadratic curve); a ratio above 8x fails.

Run: NIFT_BIN=./nift python3 benchmarks/project_dependency_scaling.py
"""
import json, os, pathlib, subprocess, sys, tempfile, time

BIN = pathlib.Path(os.environ.get('NIFT_BIN', './nift')).resolve()


def build(n, check_deps):
    with tempfile.TemporaryDirectory() as td:
        r = pathlib.Path(td)
        for d in ('.nift', 'content', 'content/posts', 'templates', 'public'):
            (r / d).mkdir(parents=True, exist_ok=True)
        (r / '.nift/config.json').write_text(json.dumps({
            'config': {'content-dir': 'content/', 'content-ext': '.md', 'output-dir': 'public/',
                       'output-ext': '.html', 'default-template': 'templates/template.html',
                       'build-threads': -1, 'incremental-mode': 'modified'}}))
        (r / 'templates/template.html').write_text(
            '<!doctype html><body>$[project.files.size()]|$[project.content.post.size()]|@content</body>')
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
        if check_deps:
            max_deps = 0
            worst = ''
            for info in (r / '.nift/public').rglob('*.info.json'):
                doc = json.loads(info.read_text())
                count = len(doc.get('dependencies', []))
                if count > max_deps:
                    max_deps = count
                    worst = str(info)
            if max_deps > 20:
                raise SystemExit(
                    f'FAIL: project page recorded {max_deps} dependencies ({worst}); '
                    f'dependency bookkeeping must stay O(1) per page, not per tracked file')
            print(f'  n={n}: max per-page dependencies = {max_deps}')
        return elapsed


def main():
    print('dependency-bookkeeping check (every page references project.*):')
    t1000 = build(1000, check_deps=True)
    t4000 = build(4000, check_deps=False)
    ratio = t4000 / t1000 if t1000 > 0 else 0
    print(f'1000 pages: {t1000:.3f}s  4000 pages: {t4000:.3f}s  ratio={ratio:.1f}x')
    if ratio > 8.0:
        raise SystemExit(f'FAIL: project-using full-build scaling is superlinear (ratio {ratio:.1f}x for 4x pages)')
    print('PASS: project-using dependency bookkeeping is O(1) and full-build scaling is approximately linear')


if __name__ == '__main__':
    main()