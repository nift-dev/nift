#!/usr/bin/env python3
"""Peak-RSS regression guard for a deterministic 10,000-page Nift project.

Linux uses /usr/bin/time -v. The guard is deliberately loose enough for normal
allocator/kernel variance but catches multi-megabyte regressions.

Outcome contract (BH2): this guard emits exactly one of PASS / FAIL / SKIP /
UNSUPPORTED on its final line and exits with the matching code. SKIP (missing
/usr/bin/time) is NOT success: a gating consumer must not treat it as green.
"""
import argparse, json, pathlib, re, statistics, subprocess, sys, tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "scripts"))
from guard_outcome import GuardSkip, finish, require_tool

ap=argparse.ArgumentParser()
ap.add_argument("--nift", required=True)
ap.add_argument("--pages", type=int, default=10000)
ap.add_argument("--runs", type=int, default=5)
ap.add_argument("--max-rss-kib", type=int, default=16384)
ap.add_argument("--time-bin", default="/usr/bin/time")
ap.add_argument("--timeout-seconds", type=float, default=120.0)
args=ap.parse_args()

try:
    time_bin = require_tool(args.time_bin, label="GNU time")
except GuardSkip as exc:
    finish(2, str(exc) + "; cannot measure peak RSS, so the guard does not run")

def fixture(root):
    (root/".nift").mkdir(parents=True)
    (root/"content").mkdir(); (root/"templates").mkdir(); (root/"public").mkdir()
    (root/".nift/config.json").write_text(json.dumps({"config":{
        "content-dir":"content/","content-ext":".html","output-dir":"public/",
        "output-ext":".html","default-template":"templates/template.html",
        "build-threads":-1,"incremental-mode":"modified","minify-exts":[]}},
        separators=(",",":")))
    (root/".nift/tracked.json").write_text(json.dumps({"tracked":[
        {"name":f"p{i}","title":f"P{i}","template":"templates/template.html"}
        for i in range(args.pages)]},separators=(",",":")))
    (root/"templates/template.html").write_text("@content\n")
    for i in range(args.pages):
        (root/"content"/f"p{i}.html").write_text(f"<p>{i}</p>\n")

def peak(root, command):
    try:
        p=subprocess.run([str(time_bin),"-v",args.nift,*command],cwd=root,
                         stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True,
                         timeout=args.timeout_seconds)
    except subprocess.TimeoutExpired as exc:
        finish(124, f"nift {command} exceeded {args.timeout_seconds}s:\n{exc.stderr or ''}")
    if p.returncode:
        finish(1, f"nift {command} failed (exit {p.returncode}):\n{p.stderr}")
    m=re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)",p.stderr)
    if not m:
        finish(1, f"could not read peak RSS from {time_bin} for {command}")
    return int(m.group(1))

with tempfile.TemporaryDirectory(prefix="nift-memory-10k-") as td:
    root=pathlib.Path(td); fixture(root)
    full=[peak(root,["build-all"]) for _ in range(args.runs)]
    noop=[peak(root,["build-updated"]) for _ in range(args.runs)]
    (root/"content"/f"p{args.pages//2}.html").write_text("<p>changed</p>\n")
    single=peak(root,["build-updated"])
    (root/"templates/template.html").write_text("<main>@content</main>\n")
    shared=peak(root,["build-updated"])

    med_full=int(statistics.median(full))
    med_noop=int(statistics.median(noop))
    print(f"{args.pages} pages peak RSS (KiB)")
    print(f"  full median:        {med_full}  samples={full}")
    print(f"  no-op median:       {med_noop}  samples={noop}")
    print(f"  single-page:        {single}")
    print(f"  shared -> all:      {shared}")
    worst=max(med_full,med_noop,single,shared)
    if worst > args.max_rss_kib:
        finish(1, f"peak RSS {worst} KiB exceeds {args.max_rss_kib} KiB guard")
    finish(0, f"all measured peaks <= {args.max_rss_kib} KiB")