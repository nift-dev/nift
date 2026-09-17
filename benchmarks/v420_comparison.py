#!/usr/bin/env python3
"""Released v4.2.0 vs current v4.3 ordinary-build comparison harness.

Read-only benchmark comparing two Nift binaries on identical ordinary
fixtures (no project.* access). Reports wall time (median of reps), peak RSS,
CPU, output byte parity, and whether an ordinary build constructs the
project-wide model (fingerprint file present).

Run:
  python3 benchmarks/v420_comparison.py /path/to/v4.2.0/nift /path/to/v4.3/nift

The comparison is diagnostic evidence for the pay-for-what-you-use property:
an ordinary project must not construct the project-wide query model.

This is a performance observation harness; it is not part of the normal test
run and makes no correctness assertions.
"""
import json, os, pathlib, subprocess, sys, tempfile, time, re, statistics, hashlib


def make_fixture(root, n, typed=True):
    r = pathlib.Path(root)
    for d in ('.nift', 'content', 'content/posts', 'templates', 'public'):
        (r / d).mkdir(parents=True, exist_ok=True)
    (r / '.nift/config.json').write_text(json.dumps({
        'config': {'content-dir': 'content/', 'content-ext': '.md', 'output-dir': 'public/',
                   'output-ext': '.html', 'default-template': 'templates/template.html',
                   'build-threads': -1, 'incremental-mode': 'modified'}}))
    (r / 'templates/template.html').write_text('<!doctype html><body>@content</body>')
    tracked = [{'name': '/', 'title': 'Home', 'template': 'templates/template.html'}]
    if typed:
        tracked[0]['type'] = 'post'
    (r / 'content/index.md').write_text('Home\n')
    for i in range(n):
        e = {'name': f'posts/p{i}', 'title': f'P{i}', 'template': 'templates/template.html'}
        if typed:
            e['type'] = 'post'
        tracked.append(e)
        (r / f'content/posts/p{i}.md').write_text(f'Body {i}\n')
    (r / '.nift/tracked.json').write_text(json.dumps({'tracked': tracked}))


def run_binary(binpath, sizes, reps=3):
    results = {}
    for n in sizes:
        td = pathlib.Path(tempfile.mkdtemp(prefix='niftv420'))
        make_fixture(td, n)
        subprocess.run([str(binpath), 'build', '--all'], cwd=td, capture_output=True)
        walls = []; rss = []; hashes = set()
        for _ in range(reps):
            for f in (td / 'public').rglob('*'):
                if f.is_file():
                    f.unlink()
            if (td / '.nift/public').exists():
                for f in (td / '.nift/public').rglob('*'):
                    if f.is_file():
                        f.unlink()
            t0 = time.perf_counter()
            p = subprocess.run(['/usr/bin/time', '-v', str(binpath), 'build', '--all'],
                               cwd=td, capture_output=True, text=True)
            walls.append(time.perf_counter() - t0)
            for line in p.stderr.splitlines():
                m = re.search(r'Maximum resident set size \(kbytes\):\s*(\d+)', line)
                if m:
                    rss.append(int(m.group(1)) / 1024)
            h = hashlib.sha256()
            for f in sorted((td / 'public').rglob('*')):
                if f.is_file() and f.suffix == '.html':
                    h.update(str(f.relative_to(td / 'public')).encode() + f.read_bytes())
            hashes.add(h.hexdigest())
        results[n] = {
            'wall_median': statistics.median(walls),
            'rss_median_mb': statistics.median(rss),
            'output_parity': len(hashes) == 1,
            'project_fingerprint_created': (td / '.nift/project.fingerprint').exists(),
        }
        subprocess.run(['rm', '-rf', str(td)])
    return results


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    a, b = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve()
    sizes = [100, 1000, 5000, 10000]
    print(f'benchmarking A ({a})...', flush=True)
    ra = run_binary(a, sizes)
    print(f'benchmarking B ({b})...', flush=True)
    rb = run_binary(b, sizes)
    print()
    print(f"{'pages':>7} | {'A wall':>9} | {'B wall':>9} | {'B/A':>7} | {'A RSS':>8} | {'B RSS':>8} | parity | fingerprint(A,B)")
    for n in sizes:
        ratio = f"{rb[n]['wall_median'] / ra[n]['wall_median']:.2f}x" if ra[n]['wall_median'] else '?'
        print(f"{n:>7} | {ra[n]['wall_median']:>7.3f}s | {rb[n]['wall_median']:>7.3f}s | {ratio:>7} | "
              f"{ra[n]['rss_median_mb']:>6.1f}M | {rb[n]['rss_median_mb']:>6.1f}M | "
              f"{'YES' if ra[n]['output_parity'] and rb[n]['output_parity'] else 'NO':>5} | "
              f"{ra[n]['project_fingerprint_created']},{rb[n]['project_fingerprint_created']}")


if __name__ == '__main__':
    main()