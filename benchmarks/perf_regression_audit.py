#!/usr/bin/env python3
"""NR12 follow-up: Nift Embed performance-regression audit (A/B).

Establishes with reproducible evidence whether the current C++ Nift + Nift
Embed architecture regressed ordinary Nift builds compared with the last
pre-Embed commit.

  baseline   = last pre-Embed Nift (aa60ab3, before Jsonic++/Embed work)
  current    = current nift-embed HEAD

Method:
  - one immutable 10k-page fixture (the established performance_10k layout)
  - correctness: build both revisions on separate copies, compare output trees
  - timing: interleave baseline/current `build-all` full 10k builds, warmup
    first, report median/min/max/mean/stddev and the delta
"""
import argparse, hashlib, json, os, pathlib, statistics, shutil, subprocess, tempfile, time

ap = argparse.ArgumentParser()
ap.add_argument("--baseline", required=True, help="baseline nift binary")
ap.add_argument("--current", required=True, help="current nift binary")
ap.add_argument("--pages", type=int, default=10000)
ap.add_argument("--samples", type=int, default=20)
ap.add_argument("--warmup", type=int, default=3)
ap.add_argument("--render-only", action="store_true",
                help="time renders with all output writes skipped (benchmark-only binaries)")
a = ap.parse_args()


def make_fixture(root: pathlib.Path, pages: int):
    root.mkdir(parents=True)
    (root / ".nift").mkdir()
    (root / "content").mkdir()
    (root / "templates").mkdir()
    (root / "public").mkdir()
    (root / ".nift/config.json").write_text(json.dumps({
        "config": {
            "content-dir": "content/", "content-ext": ".html",
            "output-dir": "public/", "output-ext": ".html",
            "default-template": "templates/template.html",
            "build-threads": -1, "incremental-mode": "modified",
            "minify-exts": [],
        }}, separators=(",", ":")))
    (root / ".nift/tracked.json").write_text(json.dumps({"tracked": [
        {"name": f"p{i}", "title": f"P{i}", "template": "templates/template.html"}
        for i in range(pages)]}, separators=(",", ":")))
    (root / "templates/template.html").write_text("@content\n")
    for i in range(pages):
        (root / "content" / f"p{i}.html").write_text(f"<p>{i}</p>\n")


def build(nift: str, root: pathlib.Path, render_only: bool) -> float:
    env = dict(os.environ)
    if render_only:
        env["NIFT_RENDER_ONLY"] = "1"
    start = time.perf_counter()
    p = subprocess.run([nift, "build-all"], cwd=root, stdout=subprocess.DEVNULL,
                       stderr=subprocess.PIPE, env=env)
    if p.returncode:
        raise SystemExit(p.stderr.decode(errors="replace"))
    return time.perf_counter() - start


def tree(root: pathlib.Path) -> dict:
    out = {}
    for path in sorted((root / "public").rglob("*")):
        if path.is_file():
            rel = path.relative_to(root / "public").as_posix()
            out[rel] = hashlib.sha256(path.read_bytes()).hexdigest()
    return out


with tempfile.TemporaryDirectory(prefix="nift-perf-audit-") as td:
    td = pathlib.Path(td)
    fixture = td / "fixture"
    make_fixture(fixture, a.pages)

    # --- correctness: identical output on both revisions (skipped for
    # render-only: no outputs are produced) ----------------------------------
    if not a.render_only:
        for name, nift in (("baseline", a.baseline), ("current", a.current)):
            copy = td / f"fix-{name}"
            shutil.copytree(fixture, copy)
            build(nift, copy, False)
            print(f"{name} output pages: {len(tree(copy))}")

        base_tree = tree(td / "fix-baseline")
        cur_tree = tree(td / "fix-current")
        same = base_tree == cur_tree
        print(f"output byte-identical: {same}")
        if not same:
            only_base = set(base_tree) - set(cur_tree)
            only_cur = set(cur_tree) - set(base_tree)
            print(f"  only baseline: {sorted(only_base)[:5]}")
            print(f"  only current:  {sorted(only_cur)[:5]}")
            differing = [k for k in set(base_tree) & set(cur_tree) if base_tree[k] != cur_tree[k]]
            print(f"  differing hashes: {len(differing)}")

    # --- timing: interleaved builds ------------------------------------------
    base_samples = []
    cur_samples = []
    for w in range(a.warmup):
        build(a.baseline, fixture, a.render_only)
        build(a.current, fixture, a.render_only)
    for i in range(a.samples):
        base_samples.append(build(a.baseline, fixture, a.render_only))
        cur_samples.append(build(a.current, fixture, a.render_only))


def report(label, samples):
    s = sorted(samples)
    mean = statistics.mean(s)
    var = sum((x - mean) ** 2 for x in s) / (len(s) - 1)
    print(f"{label}: median={statistics.median(s):.6f}s min={s[0]:.6f}s "
          f"max={s[-1]:.6f}s mean={mean:.6f}s stddev={var ** 0.5:.6f}s")
    print(f"  raw: {[f'{x:.6f}' for x in samples]}")


print("--- 10k {} (interleaved, warmup {}, {} measured) ---".format("render-only" if a.render_only else "full build", a.warmup, a.samples))
report("Pre-Embed Nift  ", base_samples)
report("Current Nift    ", cur_samples)
base_med = statistics.median(base_samples)
cur_med = statistics.median(cur_samples)
print(f"current/baseline median: {cur_med / base_med:.4f}x  "
      f"delta: {cur_med - base_med:+.6f}s  ({(cur_med - base_med) / base_med * 100:+.1f}%)")
