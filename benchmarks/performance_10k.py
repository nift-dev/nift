#!/usr/bin/env python3
import argparse, json, pathlib, shutil, statistics, subprocess, tempfile, time
ap=argparse.ArgumentParser()
ap.add_argument('--nift',required=True)
ap.add_argument('--pages',type=int,default=10000)
ap.add_argument('--full-runs',type=int,default=3)
ap.add_argument('--noop-runs',type=int,default=5)
a=ap.parse_args()

def timed(root,*args):
    start=time.perf_counter()
    p=subprocess.run([a.nift,*args],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    if p.returncode: raise SystemExit(p.stderr.decode(errors='replace'))
    return time.perf_counter()-start

with tempfile.TemporaryDirectory(prefix='nift-10k-') as td:
    root=pathlib.Path(td); (root/'.nift').mkdir(); (root/'content').mkdir(); (root/'templates').mkdir(); (root/'public').mkdir()
    (root/'.nift/config.json').write_text(json.dumps({'config':{
        'content-dir':'content/','content-ext':'.html','output-dir':'public/','output-ext':'.html',
        'default-template':'templates/template.html','build-threads':-1,'incremental-mode':'modified','minify-exts':[]}}))
    (root/'.nift/tracked.json').write_text(json.dumps({'tracked':[
        {'name':f'p{i}','title':f'P{i}','template':'templates/template.html'} for i in range(a.pages)]},separators=(',',':')))
    (root/'templates/template.html').write_text('@content\n')
    for i in range(a.pages): (root/'content'/f'p{i}.html').write_text(f'<p>{i}</p>\n')

    # Warm filesystem/page-state caches once so the reported figures describe
    # steady-state command performance rather than first-touch storage latency.
    timed(root,'build-all')
    full=[timed(root,'build-all') for _ in range(a.full_runs)]
    noop=[timed(root,'build-updated') for _ in range(a.noop_runs)]
    page=root/'content'/f'p{a.pages//2}.html'; original=page.read_text(); page.write_text(original+'<!-- edit -->\n')
    single=timed(root,'build-updated')
    page.write_text(original); timed(root,'build-updated')
    template=root/'templates/template.html'; original_template=template.read_text(); template.write_text(original_template+'\n')
    shared=timed(root,'build-updated')

    print(f'{a.pages} pages')
    print(f'full build median: {statistics.median(full):.6f}s')
    print(f'no-op updated median: {statistics.median(noop):.6f}s')
    print(f'single-page updated: {single:.6f}s')
    print(f'shared-template rebuild: {shared:.6f}s')
