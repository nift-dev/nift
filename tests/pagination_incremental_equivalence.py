#!/usr/bin/env python3
import argparse, hashlib, json, math, os, re, shutil, subprocess, tempfile
from pathlib import Path


def run(nift, cwd, *args, expect=0):
    proc = subprocess.run([nift, *args], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.returncode != expect:
        raise RuntimeError(f"{' '.join(args)} returned {proc.returncode}:\n{proc.stdout}")
    return proc.stdout


def pagination_items(project: Path) -> int:
    return (project / 'content' / 'blog.html').read_text().count('@item{')


def pagination_items_per_page(project: Path) -> int:
    tracked = json.loads((project / '.nift' / 'tracked.json').read_text())
    return int(tracked['tracked'][0]['paginate']['items-per-page'])


def assert_output_correct(project: Path, label: str) -> None:
    """The rendered pagination must be *correct*, not merely self-consistent:
    the exact expected page set must exist non-empty, and the first page's nav
    total must match ceil(items / items-per-page). A stub or a build that
    under-produces pages in both incremental and clean runs is caught here."""
    items = pagination_items(project)
    per_page = pagination_items_per_page(project)
    total = max(1, math.ceil(items / per_page))
    public = project / 'public'
    expected = ['blog.html'] + [f'blog-{i}.html' for i in range(2, total + 1)]
    for name in expected:
        page = public / name
        if not page.is_file() or page.stat().st_size == 0:
            raise RuntimeError(
                f"{label}: expected page {name!r} missing or empty "
                f"(items={items}, items-per-page={per_page})")
    if total >= 2:
        content = (public / 'blog.html').read_text()
        m = re.search(r"<nav>\d+/(\d+) ", content)
        if not m or int(m.group(1)) != total:
            raise RuntimeError(
                f"{label}: pagination nav total wrong "
                f"(expected {total}, got {m.group(1) if m else 'none'})")


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
    assert_output_correct(project, f'{label}:incremental')
    reset_generated(project)
    run(nift, project, 'build-all')
    clean = tree_hash(project / 'public')
    assert_output_correct(project, f'{label}:clean')
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
            assert_output_correct(project, f'{mode}:initial')

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
