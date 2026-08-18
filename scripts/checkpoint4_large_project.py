#!/usr/bin/env python3
from __future__ import annotations
import argparse, platform,json,pathlib,re,subprocess,tempfile,time
ap=argparse.ArgumentParser(); ap.add_argument("--nift",required=True); ap.add_argument("--pages",type=int,default=10000); ap.add_argument("--output",required=True)
a=ap.parse_args(); nift=str(pathlib.Path(a.nift).resolve())
def git_commit():
    try: return subprocess.check_output(["git","rev-parse","HEAD"],cwd=pathlib.Path(__file__).resolve().parents[1],text=True,stderr=subprocess.DEVNULL).strip()
    except Exception: return "unknown"
TIME="/usr/bin/time"
def peak(root,args):
    p=subprocess.run([TIME,"-v",nift,*args],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
    if p.returncode: raise RuntimeError(p.stderr)
    m=re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)",p.stderr); return int(m.group(1))
cases=[]
for workers,minify in [(1,False),(4,False),(-1,False),(4,True)]:
  with tempfile.TemporaryDirectory(prefix="nift-cp4-10k-") as td:
    root=pathlib.Path(td); (root/".nift").mkdir(); (root/"content").mkdir(); (root/"templates").mkdir(); (root/"public").mkdir()
    cfg={"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":workers,"incremental-mode":"modified","minify-exts":[".html"] if minify else []}}
    (root/".nift/config.json").write_text(json.dumps(cfg,separators=(",",":")))
    (root/".nift/tracked.json").write_text(json.dumps({"tracked":[{"name":f"p{i}","title":f"P{i}","template":"templates/template.html"} for i in range(a.pages)]},separators=(",",":")))
    (root/"templates/template.html").write_text("<main>@content</main>\n")
    for i in range(a.pages): (root/"content"/f"p{i}.html").write_text(f"<p> page {i} </p>\n")
    full=peak(root,["build-all"]); noop=peak(root,["build-updated"])
    (root/"content"/f"p{a.pages//2}.html").write_text("<p>changed</p>\n"); single=peak(root,["build-updated"])
    (root/"templates/template.html").write_text("<section>@content</section>\n"); shared=peak(root,["build-updated"])
    cases.append({"workers":workers,"minify":minify,"full_peak_rss_kib":full,"noop_peak_rss_kib":noop,"single_peak_rss_kib":single,"shared_peak_rss_kib":shared})
out=pathlib.Path(a.output); out.parent.mkdir(parents=True,exist_ok=True)
data={"schema_version":1,"checkpoint":"4-large-project","commit":git_commit(),"platform":platform.platform(),"pages":a.pages,"cases":cases,"pass":True}
out.write_text(json.dumps(data,indent=2)+"\n")
print("checkpoint 4 large-project matrix: PASS")
for c in cases: print(c)
print("evidence="+str(out))
