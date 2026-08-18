#!/usr/bin/env python3
from __future__ import annotations
import argparse, platform, json, os, pathlib, subprocess, tempfile, time

ap=argparse.ArgumentParser()
ap.add_argument("--nift",required=True)
ap.add_argument("--rounds",type=int,default=6)
ap.add_argument("--output",required=True)
a=ap.parse_args()
nift=str(pathlib.Path(a.nift).resolve())
def git_commit():
    try: return subprocess.check_output(["git","rev-parse","HEAD"],cwd=pathlib.Path(__file__).resolve().parents[1],text=True,stderr=subprocess.DEVNULL).strip()
    except Exception: return "unknown"

tests=[
"tests/persistence_concurrency_failure_smoke.sh",
"tests/incremental_new_features_smoke.sh",
"tests/contracts_smoke.sh",
"tests/json_schema_integration_smoke.sh",
"tests/minify_integration_smoke.sh",
"tests/requirements_smoke.sh",
"tests/path_safety_smoke.sh",
"tests/template_optional_smoke.sh",
"tests/cross_feature_smoke.sh",
]
repo=pathlib.Path(__file__).resolve().parents[1]
runs=[]
def run(cmd,cwd,expect=0,env=None):
    t=time.monotonic()
    p=subprocess.run(cmd,cwd=cwd,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
    rec={"command":cmd,"cwd":str(cwd),"exit_status":p.returncode,"expected":expect,
         "elapsed_seconds":round(time.monotonic()-t,6),
         "stdout_tail":"\n".join(p.stdout.splitlines()[-8:]),
         "stderr_tail":"\n".join(p.stderr.splitlines()[-12:])}
    runs.append(rec)
    if p.returncode!=expect:
        raise RuntimeError(json.dumps(rec,indent=2))
for r in range(a.rounds):
    with tempfile.TemporaryDirectory(prefix="nift-cp3-") as td:
        root=pathlib.Path(td)
        run([nift,"init",".html"],root)
        run([nift,"track","a",f"A{r}"],root)
        run([nift,"cp","a","b"],root)
        run([nift,"mv","b","c"],root)
        run([nift,"build-all"],root)
        run([nift,"build"],root)
        run([nift,"status"],root)
        # controlled parser failure, then repair
        tmpl=root/"templates/template.html"
        good=tmpl.read_text()
        tmpl.write_text("@input('missing-partial.html')\n@content\n")
        run([nift,"build-all"],root,expect=1)
        tmpl.write_text(good)
        run([nift,"build-updated"],root)
        run([nift,"untrack","c"],root)
        run([nift,"rm","a"],root)
        run([nift,"info-all"],root)
env=os.environ.copy(); env["NIFT_BIN"]=nift
for t in tests:
    run(["bash",t],repo,env=env)
out=pathlib.Path(a.output)
if not out.is_absolute(): out=repo/out
out.parent.mkdir(parents=True,exist_ok=True)
data={"schema_version":1,"checkpoint":"3","commit":git_commit(),"platform":platform.platform(),"rounds":a.rounds,"commands":len(runs),
      "pass":True,"runs":runs}
out.write_text(json.dumps(data,indent=2)+"\n")
print(f"checkpoint 3 core lifecycle: PASS ({len(runs)} command/test phases)")
print(f"evidence={out}")
