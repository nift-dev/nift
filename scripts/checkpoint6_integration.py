#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,os,pathlib,platform,re,subprocess,tempfile,time
ap=argparse.ArgumentParser()
ap.add_argument("--nift",required=True)
ap.add_argument("--rounds",type=int,default=60)
ap.add_argument("--pages",type=int,default=90)
ap.add_argument("--output",required=True)
ap.add_argument("--valgrind",action="store_true",
                help="Run every Nift invocation under Valgrind and fail on any memory finding.")
a=ap.parse_args(); nift=str(pathlib.Path(a.nift).resolve()); repo=pathlib.Path(__file__).resolve().parents[1]
if a.valgrind:
    import shutil
    vg=shutil.which("valgrind")
    if not vg:
        raise SystemExit("error: valgrind not found")
    command_prefix=[vg,"--leak-check=full","--show-leak-kinds=all",
                    "--errors-for-leak-kinds=definite,indirect,possible",
                    "--track-origins=yes","--error-exitcode=99",nift]
else:
    command_prefix=[nift]
valgrind_runs=[]
def commit():
    try:return subprocess.check_output(["git","rev-parse","HEAD"],cwd=repo,text=True,stderr=subprocess.DEVNULL).strip()
    except Exception:return "unknown"
def run(root,*args,ok=True):
    p=subprocess.run([*command_prefix,*args],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    combined=p.stdout+"\n"+p.stderr
    if a.valgrind:
        if p.returncode==99:
            raise RuntimeError(f"Valgrind memory finding during {args}:\n{combined}")
        m=re.search(r"ERROR SUMMARY:\s*(\d+) errors",combined)
        leak_bytes=[int(x.replace(",","")) for x in
                    re.findall(r"(?:definitely|indirectly|possibly) lost:\s*([0-9,]+) bytes",combined)]
        leaked=any(x != 0 for x in leak_bytes)
        valgrind_runs.append({"args":list(args),"exit":p.returncode,
                              "error_summary":int(m.group(1)) if m else None,
                              "reported_leak_bytes":leak_bytes,"leak_pattern":leaked})
        if m and int(m.group(1))!=0:
            raise RuntimeError(f"Valgrind reported errors during {args}")
        if leaked:
            raise RuntimeError(f"Valgrind reported leak during {args}")
    if ok and p.returncode: raise RuntimeError(f"{args} failed: {p.stderr}\n{p.stdout}")
    if not ok and p.returncode==0: raise RuntimeError(f"{args} unexpectedly succeeded")
    return p
def bump(path):
    now=time.time()+2
    os.utime(path,(now,now))
started=time.monotonic()
with tempfile.TemporaryDirectory(prefix="nift-cp6-integration-") as td:
    root=pathlib.Path(td); run(root,"init")
    cfg=json.loads((root/".nift/config.json").read_text())
    cfg["config"].update({"build-threads":4,"incremental-mode":"hybrid",
                          "minify-exts":[".html",".css"],
                          "contracts":{"routes":".nift/routes.json"}})
    (root/".nift/config.json").write_text(json.dumps(cfg,separators=(",",":"))+"\n")
    (root/".nift/routes.json").write_text('{"home":"/","docs":"/docs/"}\n')
    (root/"data").mkdir(); (root/"schemas").mkdir()
    (root/"data/state.json").write_text('{"name":"Nift","items":[{"v":1},{"v":2},{"v":3}]}\n')
    (root/"schemas/state.schema.json").write_text(
      '{"type":"object","required":["name","items"],"properties":{"name":{"type":"string"},'
      '"items":{"type":"array","items":{"type":"object","required":["v"],"properties":{"v":{"type":"integer"}}}}}}\n')
    # Shared template simultaneously exercises embedded Jsonic++ parse/schema/value
    # ownership and Minify++ HTML output ownership.
    (root/"templates/template.html").write_text(
      '@json("data/state.json", state, "schemas/state.schema.json")\n'
      '<main><a href="$[routes.home]">$[state.name]</a>'
      '@for(item : state.items){<span>$[item.v]</span>}'
      '@content</main>\n')
    tracked=json.loads((root/".nift/tracked.json").read_text())
    tracked["tracked"]=[]
    for i in range(a.pages):
        name="/" if i==0 else f"p{i}"
        tracked["tracked"].append({"name":name,"title":f"P{i}","template":"templates/template.html"})
        cp=root/"content"/("index.html" if i==0 else f"p{i}.html")
        cp.parent.mkdir(parents=True,exist_ok=True); cp.write_text(f"<section>   page {i}   </section>\n")
    # Add CSS tracked output so the same build owns a different Minify++ path.
    (root/"content/assets").mkdir(parents=True,exist_ok=True)
    (root/"templates/style.css").write_text("@content\n")
    (root/"content/assets/style.css").write_text(".x { color : red ; margin : 0  1rem ; }\n")
    tracked["tracked"].append({"name":"assets/style","title":"Style","template":"templates/style.css",
                               "content-ext":".css","output-ext":".css"})
    (root/".nift/tracked.json").write_text(json.dumps(tracked,separators=(",",":"))+"\n")
    run(root,"build-all")
    if "<section> page 0 </section>" not in (root/"public/index.html").read_text():
        raise RuntimeError("HTML was not minified through integrated build")
    if ".x{color:red;margin:0 1rem;}" not in (root/"public/assets/style.css").read_text():
        raise RuntimeError("CSS was not minified through integrated build")
    phases=0; failures=0
    for i in range(a.rounds):
        phase=i%6
        if phase==0:
            (root/"data/state.json").write_text(json.dumps({"name":f"N{i}","items":[{"v":i},{"v":i+1}]})+"\n"); bump(root/"data/state.json")
        elif phase==1:
            (root/".nift/routes.json").write_text(json.dumps({"home":f"/r/{i}/","docs":"/docs/"})+"\n"); bump(root/".nift/routes.json")
        elif phase==2:
            (root/"content/index.html").write_text(f"<section>   content {i}   </section>\n"); bump(root/"content/index.html")
        elif phase==3:
            (root/"content/assets/style.css").write_text(f".x {{ color : red ; margin : 0  {i%7+1}rem ; }}\n"); bump(root/"content/assets/style.css")
        elif phase==4:
            # Propagate a Jsonic++ parse failure through Nift, then repair it.
            good=(root/"data/state.json").read_text()
            (root/"data/state.json").write_text('{"broken":'); bump(root/"data/state.json")
            p=run(root,"build-updated",ok=False); failures+=1
            if "failed to parse" not in (p.stdout+p.stderr): raise RuntimeError("JSON failure did not propagate")
            (root/"data/state.json").write_text(good); bump(root/"data/state.json")
        else:
            # Propagate a Minify++ failure using an unsupported explicitly-minified
            # tracked output, then restore canonical tracking and continue.
            before=(root/".nift/tracked.json").read_text()
            doc=json.loads(before); doc["tracked"][-1]["output-ext"]=".txt"; doc["tracked"][-1]["minify"]=True
            (root/".nift/tracked.json").write_text(json.dumps(doc,separators=(",",":"))+"\n"); bump(root/".nift/tracked.json")
            p=run(root,"build-updated",ok=False); failures+=1
            if "minif" not in (p.stdout+p.stderr).lower(): raise RuntimeError("minifier failure did not propagate")
            (root/".nift/tracked.json").write_text(before); bump(root/".nift/tracked.json")
        run(root,"build-updated"); phases+=1
        if not (root/"public/index.html").exists() or not (root/"public/assets/style.css").exists():
            raise RuntimeError("successful recovery lost output")
    # Final clean build must remain possible after all cross-component failures.
    run(root,"build-all")
out=pathlib.Path(a.output); out.parent.mkdir(parents=True,exist_ok=True)
data={"schema_version":1,"checkpoint":"6-integration","commit":commit(),"platform":platform.platform(),
      "rounds":a.rounds,"pages":a.pages,"successful_recovery_phases":phases,
      "injected_failures":failures,"elapsed_seconds":round(time.monotonic()-started,3),"valgrind":a.valgrind,"valgrind_invocations":len(valgrind_runs),"valgrind_runs":valgrind_runs if a.valgrind else [],"pass":True}
out.write_text(json.dumps(data,indent=2)+"\n")
print(f"checkpoint 6 integration: PASS ({a.rounds} rounds, {a.pages} pages, {failures} injected failures)")
print("evidence="+str(out))
