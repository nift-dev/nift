#!/usr/bin/env python3
"""Run each probe under legacy (NIFT_VM=0), NIFT_VM=1 and NIFT_VM=require.

run_probes.py NIFT SRC_DIR OUT_DIR [extra_env=val ...]
Writes OUT_DIR/<name>.<mode>.{out,err,exit,stats} and OUT_DIR/table.md.
"""
import json, os, shutil, subprocess, sys, tempfile

nift, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
extra = dict(a.split("=", 1) for a in sys.argv[4:])
os.makedirs(out, exist_ok=True)
modes = [("legacy", "0"), ("vm1", "1"), ("req", "require")]
rows = []
for f in sorted(os.listdir(src)):
    if not f.endswith(".f"):
        continue
    name = f[:-2]
    res = {}
    for label, mode in modes:
        d = tempfile.mkdtemp(prefix="p-")
        shutil.copy(os.path.join(src, f), os.path.join(d, "p.f"))
        mod = os.path.join(src, name + ".m.nft")
        if os.path.exists(mod):
            shutil.copy(mod, os.path.join(d, "m.nft"))
        stats = os.path.join(d, "stats.json")
        env = {"PATH": "/usr/bin:/bin", "HOME": d, "LANG": "C", "LC_ALL": "C",
               "NIFT_PREVALIDATE": "off", "NIFT_VM": mode, "NIFT_VM_STATS": stats}
        env.update(extra)
        try:
            p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, timeout=60,
                               stdin=subprocess.DEVNULL)
            o, e, rc = p.stdout.decode(errors="replace"), p.stderr.decode(errors="replace"), p.returncode
        except subprocess.TimeoutExpired:
            o, e, rc = "", "TIMEOUT", 124
        e = e.replace(d, "$R")
        st = ""
        if os.path.exists(stats):
            try:
                j = json.loads(open(stats).read().splitlines()[-1])
                st = f"{j.get('outcome')}" + (f"/{j.get('reason')}" if j.get('reason') else "")
                if "heap_peak" in j:
                    st += f" peak={j.get('heap_peak')} live={j.get('heap_live')}"
            except Exception as ex:  # noqa
                st = f"badstats {ex}"
        for ext, val in (("out", o), ("err", e), ("exit", str(rc)), ("stats", st)):
            open(os.path.join(out, f"{name}.{label}.{ext}"), "w").write(val)
        res[label] = (o, e, rc, st)
        shutil.rmtree(d, ignore_errors=True)
    rows.append((name, res))

def cell(r):
    o, e, rc, st = r
    s = o.strip().replace("\n", " / ")
    if e.strip():
        s += " ⟨err: " + e.strip().replace("\n", " / ") + "⟩"
    return f"{s} [exit {rc}]".replace("|", "\\|")

with open(os.path.join(out, "table.md"), "w") as t:
    t.write("| probe | legacy (NIFT_VM=0) | NIFT_VM=1 | VM=1 stats | require | require stats | same(1) |\n")
    t.write("|---|---|---|---|---|---|---|\n")
    for name, r in rows:
        same = (r["legacy"][:3] == r["vm1"][:3])
        t.write(f"| {name} | {cell(r['legacy'])} | {cell(r['vm1'])} | {r['vm1'][3]} | {cell(r['req'])} | {r['req'][3]} | {'yes' if same else '**NO**'} |\n")
print(open(os.path.join(out, "table.md")).read())
bad = 0
for name, r in rows:
    if r["legacy"][:3] != r["vm1"][:3]:
        print(f"FAIL probe {name}: legacy and VM differ"); bad += 1
    if "internal-error" in r["vm1"][3] or "no-stats" in r["vm1"][3]:
        print(f"FAIL probe {name}: {r['vm1'][3]}"); bad += 1
if bad:
    sys.exit(1)
print(f"PASS CP12C probes: {len(rows)} cases, legacy/VM equivalent, no internal failures")
