#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, os, pathlib, platform, random, shutil, subprocess, tempfile, time

ap=argparse.ArgumentParser()
ap.add_argument("--nift",required=True)
ap.add_argument("--seeds",type=int,default=8)
ap.add_argument("--steps",type=int,default=30)
ap.add_argument("--output",required=True)
a=ap.parse_args(); NIFT=str(pathlib.Path(a.nift).resolve()); repo=pathlib.Path(__file__).resolve().parents[1]

def commit():
    try:return subprocess.check_output(["git","rev-parse","HEAD"],cwd=repo,text=True,stderr=subprocess.DEVNULL).strip()
    except Exception:return "unknown"
def run(root,*args,expect=True,timeout=30.0):
    try:
        p=subprocess.run([NIFT,*args],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout)
        rc=p.returncode; out=p.stdout; err=p.stderr
    except subprocess.TimeoutExpired as exc:
        rc=124; out=exc.stdout or ""; err=exc.stderr or ""
    if expect and rc:
        raise RuntimeError(f"{args} failed ({rc}):\n{out}\n{err}")
    p.returncode=rc; p.stdout=out; p.stderr=err
    return p
def touch(p,delta=2):
    t=time.time()+delta; os.utime(p,(t,t))
def tree(root):
    # Public output is the semantic product. Internal incremental metadata is
    # intentionally excluded: clean and incremental engines may encode history
    # differently while still producing the same site.
    out={}
    pub=root/"public"
    for p in sorted(pub.rglob("*")):
        if p.is_file():
            rel=p.relative_to(pub).as_posix()
            out[rel]=hashlib.sha256(p.read_bytes()).hexdigest()
    return out
def setup(root,mode):
    run(root,"init")
    cfg=json.loads((root/".nift/config.json").read_text())
    cfg["config"]["incremental-mode"]=mode
    cfg["config"]["build-threads"]=3
    cfg["config"]["contracts"]={"routes":".nift/routes.json"}
    (root/".nift/config.json").write_text(json.dumps(cfg,separators=(",",":"))+"\n")
    (root/".nift/routes.json").write_text('{"home":"/","docs":"/docs/"}\n')
    (root/"data").mkdir(exist_ok=True); (root/"schemas").mkdir(exist_ok=True); (root/"templates/parts").mkdir(parents=True,exist_ok=True)
    (root/"data/site.json").write_text('{"name":"Nift","items":[{"v":1},{"v":2}]}\n')
    (root/"schemas/site.schema.json").write_text('{"type":"object","required":["name","items"],"properties":{"name":{"type":"string"},"items":{"type":"array"}}}\n')
    (root/"templates/parts/shared.html").write_text("<strong>shared-0</strong>\n")
    (root/"templates/template.html").write_text(
      '@json(site, "schemas/site.schema.json", "data/site.json")\n'
      '@input("parts/shared.html")\n'
      '<a href="$[routes.home]">$[site.name]</a>\n'
      '@for(item : site.items){<i>$[item.v]</i>}\n@content\n')
    tr=json.loads((root/".nift/tracked.json").read_text())
    tr["tracked"]=[
      {"name":"/","title":"Home","template":"templates/template.html"},
      {"name":"about","title":"About","template":"templates/template.html"},
      {"name":"docs/index","title":"Docs","template":"templates/template.html"}]
    (root/".nift/tracked.json").write_text(json.dumps(tr,separators=(",",":"))+"\n")
    (root/"content/about.html").write_text("<p>about-0</p>\n")
    (root/"content/docs").mkdir(parents=True,exist_ok=True)
    (root/"content/docs/index.html").write_text("<p>docs-0</p>\n")
    (root/"content/index.html").write_text('<p>home-0 <a href="@pathto(\'about\')">about</a></p>\n')
    run(root,"build", "--all")
def mutate(root,rng,step):
    ops=["content","template","json","contract","schema","config_threads","dependency",
         "add","rename","remove","metadata"]
    op=rng.choice(ops)
    trp=root/".nift/tracked.json"
    tr=json.loads(trp.read_text())
    names=[x["name"] for x in tr["tracked"]]
    if op=="content":
        candidates=[n for n in names if n not in ("/",)]
        n=rng.choice(candidates or ["/"]); rel="index" if n=="/" else n
        p=root/"content"/(rel+".html"); p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text(f"<p>content-{step}-{rng.randrange(1_000_000)}</p>\n"); touch(p)
    elif op=="template":
        p=root/"templates/template.html"
        p.write_text('@json(site, "schemas/site.schema.json", "data/site.json")\n@input("parts/shared.html")\n'
                     f'<em>template-{step}</em><a href="$[routes.home]">$[site.name]</a>\n'
                     '@for(item : site.items){<i>$[item.v]</i>}\n@content\n'); touch(p)
    elif op=="json":
        p=root/"data/site.json"; p.write_text(json.dumps({"name":f"N{step}","items":[{"v":step},{"v":step+1}]})+"\n"); touch(p)
    elif op=="contract":
        p=root/".nift/routes.json"; p.write_text(json.dumps({"home":f"/r/{step}/","docs":"/docs/"})+"\n"); touch(p)
    elif op=="schema":
        p=root/"schemas/site.schema.json"
        p.write_text('{"type":"object","required":["name","items"],"properties":{"name":{"type":"string","minLength":1},"items":{"type":"array","maxItems":9}}}\n'); touch(p)
    elif op=="config_threads":
        p=root/".nift/config.json"; d=json.loads(p.read_text()); d["config"]["build-threads"]=1+(step%4); p.write_text(json.dumps(d,separators=(",",":"))+"\n"); touch(p)
    elif op=="dependency":
        p=root/"templates/parts/shared.html"; p.write_text(f"<strong>shared-{step}</strong>\n"); touch(p)
    elif op=="add":
        n=f"generated/p{step}"
        if n not in names:
            p=root/"content"/(n+".html"); p.parent.mkdir(parents=True,exist_ok=True); p.write_text(f"<p>added-{step}</p>\n")
            r=run(root,"track",n,f"Added {step}","templates/template.html",expect=False)
            if r.returncode: raise RuntimeError("track failed: "+r.stdout+r.stderr)
    elif op=="rename":
        candidates=[n for n in names if n not in ("/","about","docs/index") and n.startswith("generated/")]
        if candidates:
            old=rng.choice(candidates); new=old+f"-r{step}"
            r=run(root,"mv",old,new,expect=False)
            if r.returncode: raise RuntimeError("mv failed: "+r.stdout+r.stderr)
        else:
            return mutate(root,rng,step+10000)
    elif op=="remove":
        candidates=[n for n in names if n not in ("/","about","docs/index") and n.startswith("generated/")]
        if candidates:
            r=run(root,"rm",rng.choice(candidates),expect=False)
            if r.returncode: raise RuntimeError("rm failed: "+r.stdout+r.stderr)
        else:
            return mutate(root,rng,step+20000)
    elif op=="metadata":
        idx=rng.randrange(len(tr["tracked"])); tr["tracked"][idx]["title"]=f"Title {step}"
        trp.write_text(json.dumps(tr,separators=(",",":"))+"\n"); touch(trp)
    return op

started=time.monotonic(); cases=[]; total=0
with tempfile.TemporaryDirectory(prefix="nift-cp7-") as td:
    td=pathlib.Path(td)
    for mode in ("modified","hash","hybrid"):
      for seed in range(a.seeds):
        root=td/f"{mode}-{seed}"; root.mkdir(); setup(root,mode)
        rng=random.Random(seed*1009+{"modified":1,"hash":2,"hybrid":3}[mode])
        ops=[]
        for step in range(a.steps):
            op=mutate(root,rng,step); ops.append(op)
            run(root,"build")
            inc=tree(root)
            # A clean rebuild in the same logical project is the oracle. Remove
            # generated output + page build metadata, then build every tracked entry.
            tracked_now=json.loads((root/".nift/tracked.json").read_text())["tracked"]
            cfg_now=json.loads((root/".nift/config.json").read_text())["config"]
            out_ext=cfg_now.get("output-ext",".html")
            for ent in tracked_now:
                name=ent["name"]
                rel="index" if name=="/" else name
                ext=ent.get("output-ext",out_ext)
                opath=root/"public"/(rel+ext)
                if opath.exists(): opath.unlink()
            info=root/".nift/public"
            if info.exists(): shutil.rmtree(info)
            run(root,"build", "--all")
            clean=tree(root)
            total+=1
            if inc!=clean:
                missing=sorted(set(clean)-set(inc)); extra=sorted(set(inc)-set(clean))
                changed=sorted(k for k in set(inc)&set(clean) if inc[k]!=clean[k])
                raise RuntimeError(f"equivalence failure mode={mode} seed={seed} step={step} op={op} missing={missing} extra={extra} changed={changed}")
        cases.append({"mode":mode,"seed":seed,"steps":a.steps,"operations":ops})
out=pathlib.Path(a.output); out.parent.mkdir(parents=True,exist_ok=True)
data={"schema_version":1,"checkpoint":"7-incremental-equivalence","commit":commit(),"platform":platform.platform(),
      "modes":["modified","hash","hybrid"],"seeds_per_mode":a.seeds,"steps_per_seed":a.steps,
      "comparisons":total,"cases":cases,"elapsed_seconds":round(time.monotonic()-started,3),"pass":True}
out.write_text(json.dumps(data,indent=2)+"\n")
print(f"checkpoint 7 incremental equivalence: PASS ({total} complete output-tree comparisons)")
print("evidence="+str(out))
