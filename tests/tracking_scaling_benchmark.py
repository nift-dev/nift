#!/usr/bin/env python3
"""Regression benchmark for tracked-project loading.

Builds synthetic tracked.json files and measures `nift info --names`, which opens
and validates the project without rendering pages. The ratio guard catches the
historical O(n^2) duplicate/collision validation regression.
"""
import argparse, json, pathlib, shutil, subprocess, tempfile, time, statistics

ap=argparse.ArgumentParser()
ap.add_argument("--nift", required=True)
ap.add_argument("--small", type=int, default=2000)
ap.add_argument("--large", type=int, default=10000)
ap.add_argument("--runs", type=int, default=5)
ap.add_argument("--max-ratio", type=float, default=8.0,
                help="maximum large/small runtime ratio; 10k/2k linear scaling is ~5x")
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

def measure(n):
    with tempfile.TemporaryDirectory(prefix=f"nift-track-{n}-") as td:
        root=pathlib.Path(td); fixture(root,n)
        values=[]
        for _ in range(args.runs):
            t=time.perf_counter()
            p=subprocess.run([args.nift,"info", "--names"],cwd=root,
                             stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            if p.returncode: raise SystemExit(p.stderr.decode(errors="replace"))
            values.append(time.perf_counter()-t)
        return statistics.median(values)

small=measure(args.small); large=measure(args.large); ratio=large/small
print(f"tracking load {args.small}: {small:.6f}s")
print(f"tracking load {args.large}: {large:.6f}s")
print(f"ratio: {ratio:.2f}x")
if ratio > args.max_ratio:
    raise SystemExit(f"FAIL: tracking load scales too steeply ({ratio:.2f}x > {args.max_ratio:.2f}x)")
print("PASS: tracked-project loading remains near-linear")


def expect_invalid(entries,label):
    with tempfile.TemporaryDirectory(prefix='nift-track-invalid-') as td:
        root=pathlib.Path(td); fixture(root,0)
        (root/'.nift/tracked.json').write_text(json.dumps({'tracked':entries},separators=(',',':')))
        p=subprocess.run([args.nift,'info', '--names'],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        if p.returncode==0: raise SystemExit(f'FAIL: {label} was accepted')

expect_invalid([
    {'name':'x','title':'X','template':'templates/template.html'},
    {'name':'x','title':'X2','template':'templates/template.html'}], 'duplicate tracked name')
expect_invalid([
    {'name':'/','title':'Home','template':'templates/template.html'},
    {'name':'index','title':'Index','template':'templates/template.html'}], 'derived content/output collision')
print('PASS: indexed tracking validation preserves duplicate/collision rejection')
