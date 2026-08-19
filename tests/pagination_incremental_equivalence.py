#!/usr/bin/env python3
import argparse, hashlib, json, os, shutil, subprocess, tempfile
from pathlib import Path


def run(nift, cwd, *args, expect=0):
    proc = subprocess.run([nift, *args], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.returncode != expect:
        raise RuntimeError(f"{' '.join(args)} returned {proc.returncode}:\n{proc.stdout}")
    return proc.stdout


def tree_hash(root: Path):
    result = {}
    if not root.exists():
        return result
    for path in sorted(p for p in root.rglob('*') if p.is_file()):
        result[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def reset_generated(project: Path):
    public = project / 'public'
    if public.exists():
        shutil.rmtree(public)
    public.mkdir()
    meta = project / '.nift' / 'public'
    if meta.exists():
        shutil.rmtree(meta)


def write_project(project: Path, mode: str):
    (project / '.nift').mkdir(parents=True)
    (project / 'content').mkdir()
    (project / 'templates').mkdir()
    (project / 'public').mkdir()
    (project / '.nift' / 'config.json').write_text(json.dumps({"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
        "output-ext": ".html", "default-template": "templates/template.html",
        "build-threads": 4, "incremental-mode": mode
    }}))
    (project / '.nift' / 'tracked.json').write_text(json.dumps({"tracked": [{
        "name": "blog", "title": "Blog", "template": "templates/template.html",
        "paginate": {"items-per-page": 2}
    }]}))
    (project / 'templates' / 'template.html').write_text('<main>@content</main>\n')
    (project / 'content' / 'blog.paginate.html').write_text(
        '<section>$[paginate.items]</section><nav>$[paginate.current]/$[paginate.total] '
        '@if(!paginate.first){<a href="@pathtopage($[paginate.previous])">p</a>}'
        '@if(!paginate.last){<a href="@pathtopage($[paginate.next])">n</a>}</nav>\n')
    (project / 'content' / 'blog.html').write_text('@item{a}@item{b}@item{c}@item{d}@item{e}@paginate\n')


def compare_incremental_to_clean(nift: str, project: Path, label: str):
    run(nift, project, 'build')
    inc = tree_hash(project / 'public')
    reset_generated(project)
    run(nift, project, 'build-all')
    clean = tree_hash(project / 'public')
    if inc != clean:
        raise RuntimeError(f"{label}: incremental/clean mismatch\nincremental={inc}\nclean={clean}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    args = ap.parse_args()
    nift = str(Path(args.nift).resolve())
    with tempfile.TemporaryDirectory(prefix='nift-pagination-equivalence.') as tmp:
        base = Path(tmp)
        comparisons = 0
        for mode in ('modified', 'hash', 'hybrid'):
            project = base / mode
            project.mkdir()
            write_project(project, mode)
            run(nift, project, 'build-all')

            # Content changes without changing page count.
            (project / 'content' / 'blog.html').write_text('@item{a}@item{B}@item{c}@item{d}@item{e}@paginate\n')
            compare_incremental_to_clean(nift, project, f'{mode}:content')
            comparisons += 1

            # Shrink page count; stale outputs must disappear identically.
            (project / 'content' / 'blog.html').write_text('@item{a}@item{b}@paginate\n')
            compare_incremental_to_clean(nift, project, f'{mode}:shrink')
            comparisons += 1

            # Grow page count again.
            (project / 'content' / 'blog.html').write_text('@item{a}@item{b}@item{c}@item{d}@item{e}@item{f}@paginate\n')
            compare_incremental_to_clean(nift, project, f'{mode}:grow')
            comparisons += 1

            # Optional conventional separator appearance/disappearance.
            (project / 'content' / 'blog.separator.html').write_text('|$[paginate.current]|')
            compare_incremental_to_clean(nift, project, f'{mode}:separator-add')
            comparisons += 1
            (project / 'content' / 'blog.separator.html').unlink()
            compare_incremental_to_clean(nift, project, f'{mode}:separator-remove')
            comparisons += 1

            # Tracking metadata change must invalidate the whole pagination set.
            tracked_path = project / '.nift' / 'tracked.json'
            tracked = json.loads(tracked_path.read_text())
            tracked['tracked'][0]['paginate']['items-per-page'] = 3
            tracked_path.write_text(json.dumps(tracked))
            compare_incremental_to_clean(nift, project, f'{mode}:items-per-page')
            comparisons += 1

        print(f'Pagination incremental equivalence passed: {comparisons} comparisons')

if __name__ == '__main__':
    main()
