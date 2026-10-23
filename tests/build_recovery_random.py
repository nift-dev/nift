#!/usr/bin/env python3
"""feature-build-recovery (D-020) bounded randomized tester (ported from the
independent review's rnd.py). Random sequences of edits, breakages (missing
dependency, expression error, failing scripts, pagination template error),
fixes, data changes, plain/--all/targeted builds, SIGKILLs at every named
point (NIFT_TEST_CRASH_AT) and incremental-mode switches. Invariants:
an uninterrupted build never leaves .nift/.unfinished; a build with no broken
source succeeds; a copy of the project with every breakage fixed builds
cleanly and is byte-identical to a from-scratch build ("virtual fix"); with
nothing broken, a build succeeds, is followed by a no-op, matches a clean
build and leaves no failed list or backup files.

Bounded by default (RECOVERY_RANDOM_STEPS=80 steps x RECOVERY_RANDOM_SEEDS=1
seed x 3 modes); raise the environment variables for a soak run.
"""
import json, os, random, shutil, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_recovery import run, write, project, snapshot, oracle, marker, NIFT

SCR = None


def _without_hashes(data):
    try:
        doc = json.loads(data)
        if isinstance(doc, dict) and "dependency-hashes" in doc:
            doc.pop("dependency-hashes", None)
            return json.dumps(doc, sort_keys=True, separators=(",", ":")).encode()
    except Exception:
        pass
    return data


def diffclean(r):
    got, want = snapshot(r), oracle(r, SCR)
    mode = json.loads((r / ".nift/config.json").read_text())["config"].get("incremental-mode", "modified")
    if mode == "modified":
        got = {k: (_without_hashes(v) if k.endswith(".info.json") else v) for k, v in got.items()}
        want = {k: (_without_hashes(v) if k.endswith(".info.json") else v) for k, v in want.items()}
    extra = sorted(set(p.relative_to(r).as_posix() for p in (r / "public").rglob("*") if p.is_file()) - set(want))
    return sorted(k for k in set(got) | set(want) if got.get(k) != want.get(k)), extra


PAGES=["plist","a","b","c","list","post","legacy","custom","base","child"]
POINTS_E=["acquired","reconciled","pre-hooks","pages-done","post-hooks","failed-list","repair-sweep"]
POINTS_P=["page-start","page-rendered","page-output-partial","page-output","page-post","page-info","page-done","page-failed"]
GOOD_SCRIPTS={"scripts/lpost.f":'print("lp ok")\n',"scripts/post.f":'print("post ok")\n',"scripts/legacy.f":'print("legacy ok")\n',
 "scripts/custom.f":'out := file(getenv("NIFT_HOOK_OUTPUT"))\nout.open("w")\nout.write("custom-1")\nout.save()\nout.close()\n'}
BAD_SCRIPT={"scripts/lpost.f":'nosuchfunction_xyz()\n',"scripts/post.f":'nosuchfunction_xyz()\n',"scripts/legacy.f":'nosuchfunction_xyz()\n',
 "scripts/custom.f":'out := file(getenv("NIFT_HOOK_OUTPUT"))\nout.open("w")\nout.write("PARTIAL")\nout.save()\nout.close()\nnosuchfunction_xyz()\n'}


def _mtimes(root):
    """mtime_ns of every file under the project except outputs, read from disk."""
    out = {}
    for p in root.rglob("*"):
        if p.is_file() and "public" != p.relative_to(root).parts[0]:
            out[p.relative_to(root).as_posix()] = p.stat().st_mtime_ns
    return out


def _equal_tick(before, out):
    """True only if every page the build rebuilt was rebuilt solely because a
    dependency's mtime EQUALS that page's recorded build timestamp (its
    page-info mtime), both read from disk before the build."""
    page, ok, seen = None, True, False
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("built "):
            page = line[len("built "):]
            seen = True
        elif line.startswith("↳ "):
            reason = line[2:]
            prefix = "dependency changed: "
            info = before.get(f".nift/public/{page}.info.json") if page else None
            dep = before.get(reason[len(prefix):]) if reason.startswith(prefix) else None
            if info is None or dep is None or dep != info:
                ok = False
    return seen and ok


def settles(root, stats):
    """The build after a successful build must be a no-op. Exactly one extra
    rebuild is allowed, and only when the tester itself confirms the
    equal-timestamp condition from the files on disk: every rebuilt page's
    every reason is "dependency changed: X" with mtime(X) == mtime(page-info)
    (modified/hybrid rebuild conservatively on equal timestamps - the
    pre-existing CP01 rule, identical before this branch). The build after
    that must be a no-op. Allowances are counted."""
    for attempt in range(2):
        before = _mtimes(root)
        rc, out = run(root, "build")
        if rc == 0 and "up to date" in out:
            return True
        if attempt == 0 and rc == 0 and _equal_tick(before, out):
            stats["equal_tick_allowances"] = stats.get("equal_tick_allowances", 0) + 1
            continue
        print(out[-400:])
        return False
    return False


def main(mode, steps, seed):
    rng=random.Random(seed)
    r=SCR/f"rnd-{mode}-{seed}"
    if r.exists(): shutil.rmtree(r)
    project(r,mode)
    t=json.loads((r/".nift/tracked.json").read_text())
    t["tracked"].append({"name":"plist","title":"PL","template":"templates/t.html","paginate":{"items-per-page":2},"post-build":"scripts/lpost.f"})
    (r/".nift/tracked.json").write_text(json.dumps(t))
    write(r,"content/plist.html","@json(d, 'data/items.json')\n@paginate\n@for(x : d.items){@item{<i>$[x]</i>}}\n")
    write(r,"content/plist.paginate.html","<s>$[paginate.items]</s>$[paginate.current]\n")
    write(r,"scripts/lpost.f",GOOD_SCRIPTS["scripts/lpost.f"])
    rc,out=run(r,"build"); assert rc==0,out
    broken=set()   # sources currently broken
    stats=dict(steps=0,builds=0,failbuilds=0,kills=0,recoveries=0,compares=0,problems=0,targeted=0,all=0)
    probs=[]
    def prob(msg):
        stats["problems"]+=1; probs.append(msg); print("PROBLEM",mode,seed,msg,flush=True)
    content_ver={p:0 for p in PAGES if p not in ("list","plist")}
    for step in range(steps):
        stats["steps"]+=1
        op=rng.choice(["edit","edit","break","break","fix","data","items","build","build","kill","kill","target","all","fix","mode"])
        if op=="edit":
            p=rng.choice(list(content_ver)); content_ver[p]+=1
            if p not in broken: write(r,f"content/{p}.html",f"<p>{p} v{content_ver[p]}</p>@input(\"data/shared.txt\")\n")
        elif op=="break":
            kind=rng.choice(["missing","expr","script","paginate"])
            if kind in("missing","expr"):
                p=rng.choice(list(content_ver)); broken.add(p)
                write(r,f"content/{p}.html", f"<p>{p}</p>@input(\"data/missing.txt\")\n" if kind=="missing" else f"<p>{p}</p>$[1 +]\n")
            elif kind=="script":
                s=rng.choice(list(BAD_SCRIPT)); broken.add(s); write(r,s,BAD_SCRIPT[s])
            else:
                broken.add("pag"); write(r,"content/list.paginate.html","<section>$[paginate.items]</section>$[1 +]\n")
        elif op=="fix" and broken:
            x=rng.choice(sorted(broken)); broken.discard(x)
            if x in GOOD_SCRIPTS: write(r,x,GOOD_SCRIPTS[x])
            elif x=="pag": write(r,"content/list.paginate.html","<section>$[paginate.items]</section>$[paginate.current]/$[paginate.total]\n")
            else: content_ver[x]+=1; write(r,f"content/{x}.html",f"<p>{x} v{content_ver[x]}</p>\n")
        elif op=="data":
            write(r,"data/shared.txt",f"shared-{step}\n")
        elif op=="items":
            write(r,"data/items.json",json.dumps({"items":[f"x{j}" for j in range(rng.randint(0,9))]}))
        elif op in("build","all","target"):
            args=[] if op=="build" else (["--all"] if op=="all" else rng.sample(PAGES,rng.randint(1,3)))
            stats["builds"]+=1; stats["targeted"]+=op=="target"; stats["all"]+=op=="all"
            had=marker(r)
            rc,out=run(r,"build",*args)
            if had: stats["recoveries"]+=1
            if rc!=0: stats["failbuilds"]+=1
            if marker(r): prob(f"step{step} marker after uninterrupted build {args} rc={rc}: {out[-300:]}")
            if rc!=0 and not broken: prob(f"step{step} build {args} failed with no broken source: {out[-400:]}")
        elif op=="kill":
            pt=rng.choice(POINTS_E+POINTS_P*2)
            spec=pt+(":"+rng.choice(PAGES) if pt in POINTS_P and rng.random()<0.7 else "")
            args=rng.choice([[],[],["--all"],[rng.choice(PAGES)]])
            rc,out=run(r,"build",*args,env={"NIFT_TEST_CRASH_AT":spec})
            if rc in(-9,137):
                stats["kills"]+=1
                if not marker(r): prob(f"step{step} killed at {spec} but no marker")
        elif op=="mode":
            c=json.loads((r/".nift/config.json").read_text()); c["config"]["incremental-mode"]=rng.choice(["modified","hash","hybrid"]); (r/".nift/config.json").write_text(json.dumps(c))
        if broken and rng.random()<0.5:
            v=SCR/f"virt-{mode}-{seed}"
            if v.exists(): shutil.rmtree(v)
            subprocess.run(["cp","-a",str(r),str(v)],check=True)
            for x in broken:
                if x in GOOD_SCRIPTS: write(v,x,GOOD_SCRIPTS[x])
                elif x=="pag": write(v,"content/list.paginate.html","<section>$[paginate.items]</section>$[paginate.current]/$[paginate.total]\n")
                else: write(v,f"content/{x}.html",f"<p>{x} fixed</p>\n")
            rc,out=run(v,"build")
            if rc!=0 or marker(v): prob(f"step{step} virtual-fix build rc={rc} marker={marker(v)}: {out[-400:]}")
            else:
                d,extra=diffclean(v); stats["vcompares"]=stats.get("vcompares",0)+1
                if d or extra: prob(f"step{step} virtual-fix differs from clean: {d[:6]} extra {extra[:6]} broken={sorted(broken)}")
                if not settles(v, stats): prob(f"step{step} virtual not no-op")
            shutil.rmtree(v)
        # comparison point: when nothing broken, plain build must succeed and match a clean build
        if not broken and rng.random()<0.5:
            rc,out=run(r,"build")
            if rc!=0 or marker(r): prob(f"step{step} clean-sources build rc={rc} marker={marker(r)}: {out[-400:]}"); continue
            if not settles(r, stats): prob(f"step{step} not a no-op after build")
            d,extra=diffclean(r); stats["compares"]+=1
            if d or extra: prob(f"step{step} differs from clean build: {d[:6]} extra {extra[:6]}")
            if (r/".nift/failed.json").exists(): prob(f"step{step} failed.json remains: {(r/'.nift/failed.json').read_text()}")
            if list(r.rglob("*.nift-tmp-*")): prob(f"step{step} tmp files remain")
    print(mode,seed,stats,flush=True)
    return stats,probs


def entry():
    global SCR
    steps = int(os.environ.get("RECOVERY_RANDOM_STEPS", "80"))
    seeds = int(os.environ.get("RECOVERY_RANDOM_SEEDS", "1"))
    base_seed = int(os.environ.get("RECOVERY_RANDOM_SEED", "20261018"))
    problems = []
    allowances = 0
    with tempfile.TemporaryDirectory(prefix="nift-recovery-rnd-") as td:
        SCR = Path(td)
        for m, mode in enumerate(("modified", "hash", "hybrid")):
            for k in range(seeds):
                stats, probs = main(mode, steps, base_seed + 1000 * m + k)
                problems += probs
                allowances += stats.get("equal_tick_allowances", 0)
    if problems:
        print(f"build recovery randomized: {len(problems)} problems; "
              f"{allowances} confirmed equal-timestamp extra rebuild(s) allowed")
        return 1
    print(f"PASS build recovery randomized: {steps} steps x {seeds} seeds x 3 modes; "
          f"{allowances} confirmed equal-timestamp extra rebuild(s) allowed")
    return 0


if __name__ == "__main__":
    sys.exit(entry())
