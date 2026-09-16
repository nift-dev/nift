#!/usr/bin/env python3
"""CP47 v4.3 performance campaign: focused v4.3 language workloads.

Each workload is a single page amplifying one v4.3 feature; `nift build --all`
(cold rebuild) is timed over repeated samples. Reports min/p25/median/p75/max,
mean, stddev and raw samples. Map-typed-entry iteration is sanity-checked by
comparing numeric-key and string-key map @for timing (no per-key formatting
should be observable as a gross regression).
"""
import pathlib, shutil, statistics, subprocess, tempfile, time, json, os

NIFT = os.environ.get("NIFT_BIN")
assert NIFT, "NIFT_BIN required"

def make_project(root, content):
    (root/".nift").mkdir(parents=True); (root/"content").mkdir(); (root/"templates").mkdir(); (root/"public").mkdir()
    (root/".nift/config.json").write_text(json.dumps({"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified","minify-exts":[]}}))
    (root/".nift/tracked.json").write_text(json.dumps({"tracked":[{"name":"/","title":"b","template":"templates/template.html"}]}))
    (root/"templates/template.html").write_text("@content\n")
    (root/"content/index.html").write_text(content)

def gen_array_push(n):
    return "$[a := []]\n" + "$[a.push(" + str(0) + ")]\n"*1 + "\n".join("$[a.push(%d)]"%i for i in range(n)) + "\n$[a.size()]\n"

def gen_map_string(n):
    lines=["$[m := map()]"]
    for i in range(n): lines.append("$[m.set(\"k%d\",%d)]"%(i,i))
    lines.append("@for((k,v) : m){$[v]}|\n")
    return "\n".join(lines)

def gen_map_int(n):
    lines=["$[m := map()]"]
    for i in range(n): lines.append("$[m.set(%d,%d)]"%(i,i))
    lines.append("@for((k,v) : m){$[v]}|\n")
    return "\n".join(lines)

def gen_map_int_iter(n):
    lines=["$[m := map()]"]
    for i in range(n): lines.append("$[m.set(%d,%d)]"%(i,i))
    lines.append("@for((k,v) : m){$[v]}|\n")
    return "\n".join(lines)

def gen_sorted_map(n):
    lines=["$[sm := sorted_map()]"]
    for i in range(n,0,-1): lines.append("$[sm.set(\"k%d\",%d)]"%(i,i))
    lines.append("@for((k,v) : sm){$[v]}|\n")
    return "\n".join(lines)

def gen_set(n):
    lines=["$[s := set()]"]
    for i in range(n): lines.append("$[s.add(%d)]"%i)
    lines.append("$[s.size()]\n")
    return "\n".join(lines)

def gen_sorted_set(n):
    lines=["$[s := sorted_set()]"]
    for i in range(n,0,-1): lines.append("$[s.add(%d)]"%i)
    lines.append("$[s.size()]\n")
    return "\n".join(lines)

def gen_higher_order(n):
    return "$[a := []]\n"+"\n".join("$[a.push(%d)]"%i for i in range(n)) + "\n$[dm := a.map(x => x * 2)]$[dm.size()],$[ev := a.filter(x => x % 2 == 0)]$[ev.size()],$[a.reduce((acc,x) => acc + x, 0)]\n"

def gen_fn_invoke(n):
    return "@fn(twice(x)){ return x * 2 }\n" + "$[twice(1)]"*n + "\n"

def gen_lambda_invoke(n):
    return "$[f := x => x * 2]\n" + "$[f(1)]"*n + "\n"

def gen_closure(n):
    return "@fn(mk(x)){ n := x; return () => n++ }\n" + "$[f := mk(0)]\n" + "$[f()]"*n + "\n"

WORKLOADS = {
    "array_push_10k": gen_array_push(10000),
    "map_string_5k": gen_map_string(5000),
    "map_int_5k": gen_map_int(5000),
    "map_int_iter_5k": gen_map_int_iter(5000),
    "sorted_map_5k": gen_sorted_map(5000),
    "set_5k": gen_set(5000),
    "sorted_set_5k": gen_sorted_set(5000),
    "higher_order_5k": gen_higher_order(5000),
    "fn_invoke_10k": gen_fn_invoke(10000),
    "lambda_invoke_10k": gen_lambda_invoke(10000),
    "closure_10k": gen_closure(10000),
}

def build(root):
    start=time.perf_counter()
    p=subprocess.run([NIFT,"build","--all"],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    if p.returncode: raise SystemExit("build failed: "+p.stderr.decode(errors="replace")[:200])
    return time.perf_counter()-start

results={}
for name, content in WORKLOADS.items():
    with tempfile.TemporaryDirectory(prefix="cp47-") as td:
        root=pathlib.Path(td)/"w"; make_project(root, content)
        for _ in range(2): build(root)
        samples=[build(root) for _ in range(15)]
    s=sorted(samples); m=statistics.mean(s)
    results[name]={"n":15,"min":round(s[0],6),"p25":round(s[3],6),"median":round(statistics.median(s),6),"p75":round(s[11],6),"max":round(s[-1],6),"mean":round(m,6),"stddev":round((sum((x-m)**2 for x in s)/14)**0.5,6)}
    print(f"{name:<20} median={results[name]['median']:.6f}s  min={results[name]['min']:.6f}  max={results[name]['max']:.6f}  stddev={results[name]['stddev']:.6f}")
print(json.dumps(results, indent=2))