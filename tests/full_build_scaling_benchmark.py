#!/usr/bin/env python3
"""Regression benchmark for full-build output scaling.

This specifically protects against doing directory-wide recovery or validation
once per generated file. The Checkpoint 8 transactional-writer regression made
a flat N-page build O(N^2) by scanning the output directory before every write.
Fixture creation is excluded from timing. Each measured `nift build-all` toggles
the shared template so every generated page changes; the guard therefore measures
the transactional output path rather than an identical-output fast path.
"""
import argparse, json, pathlib, statistics, subprocess, tempfile, time

ap=argparse.ArgumentParser()
ap.add_argument("--nift", required=True)
ap.add_argument("--small", type=int, default=1000)
ap.add_argument("--large", type=int, default=4000)
ap.add_argument("--runs", type=int, default=3)
ap.add_argument("--max-ratio", type=float, default=7.0,
                help="maximum large/small full-build ratio; 4k/1k linear scaling is ~4x")
args=ap.parse_args()

def fixture(root,n):
    (root/".nift").mkdir(parents=True)
    (root/"content").mkdir(); (root/"templates").mkdir(); (root/"public").mkdir()
    (root/".nift/config.json").write_text(json.dumps({"config":{
        "content-dir":"content/","content-ext":".html","output-dir":"public/",
        "output-ext":".html","default-template":"templates/template.html",
        "build-threads":1,"incremental-mode":"modified","minify-exts":[]}}))
    (root/".nift/tracked.json").write_text(json.dumps({"tracked":[
        {"name":f"p{i}","title":f"P{i}","template":"templates/template.html"}
        for i in range(n)]},separators=(",",":")))
    (root/"templates/template.html").write_text("@content\\n")
    for i in range(n): (root/"content"/f"p{i}.html").write_text(f"<p>{i}</p>\\n")

def run_build(root):
    started=time.perf_counter()
    p=subprocess.run([args.nift,"build-all"],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    if p.returncode: raise SystemExit(p.stderr.decode(errors="replace"))
    return time.perf_counter()-started

def measure(n):
    with tempfile.TemporaryDirectory(prefix=f"nift-full-scale-{n}-") as td:
        root=pathlib.Path(td); fixture(root,n)
        template=root/"templates/template.html"
        # Establish generated outputs first. Then alternate two rendering
        # variants so every timed run rewrites all outputs through the safe
        # transactional path. The historical bug scanned an already-populated
        # directory before every one of these rewrites.
        run_build(root)
        values=[]
        for i in range(args.runs):
            template.write_text(("<main class='a'>@content</main>\n" if i % 2 == 0
                                 else "<main class='b'>@content</main>\n"))
            values.append(run_build(root))
        return statistics.median(values)

small=measure(args.small); large=measure(args.large); ratio=large/small
print(f"full build {args.small}: {small:.6f}s")
print(f"full build {args.large}: {large:.6f}s")
print(f"ratio: {ratio:.2f}x")
if ratio > args.max_ratio:
    raise SystemExit(f"FAIL: full-build output work scales too steeply ({ratio:.2f}x > {args.max_ratio:.2f}x)")
print("PASS: full-build output work remains near-linear")
