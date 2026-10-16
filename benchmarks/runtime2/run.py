#!/usr/bin/env python3
"""Runtime 2.0 baseline suite.

Usage:
  python3 benchmarks/runtime2/run.py --nift ./nift [--groups startup,kernels,ops,process,async,site]
                                     [--out FILE.jsonl] [--runs N]

Every workload checks its output against a golden value. Gates use the `ops`
group (callgrind marginal instructions, deterministic); wall times are reported
with a host fingerprint. Python-only, stdlib-only. Needs valgrind for `ops` and
`site-profile`, bash and python3 for comparisons.
"""
import argparse, json, os, platform, re, shutil, statistics, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
GROUPS = ["startup", "kernels", "ops", "process", "async", "site"]


def host():
    cpu = ""
    try:
        cpu = next(l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo") if l.startswith("model name"))
    except Exception:
        pass
    return {"platform": platform.platform(), "cpu": cpu, "nproc": os.cpu_count(),
            "python": platform.python_version(), "time": time.strftime("%Y-%m-%dT%H:%M:%S%z")}


def run(cmd, cwd=None, stdin=None):
    t = time.perf_counter()
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=False,
                       input=stdin)
    return time.perf_counter() - t, p


def median_wall(cmd, runs, golden=None, cwd=None):
    times = []
    out = ""
    for _ in range(runs):
        dt, p = run(cmd, cwd)
        out = p.stdout.decode(errors="replace").strip()
        if p.returncode != 0:
            raise RuntimeError(f"{cmd} failed: {p.stderr.decode(errors='replace')[:400]}")
        if golden is not None and out.splitlines()[-1:] != [golden]:
            raise RuntimeError(f"{cmd} output {out[-80:]!r} != golden {golden!r}")
        times.append(dt)
    return {"median_s": round(statistics.median(times), 5), "min_s": round(min(times), 5), "n": runs}


def callgrind_ir(cmd, cwd=None):
    fd, outp = tempfile.mkstemp(suffix=".cg")
    os.close(fd)
    p = subprocess.run(["valgrind", "--tool=callgrind", f"--callgrind-out-file={outp}", *cmd], cwd=cwd,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    os.unlink(outp)
    m = re.search(r"Collected : (\d+)", p.stderr)
    if not m:
        raise RuntimeError(p.stderr[-600:])
    return int(m.group(1)), p.stdout.strip()


# ---------------------------------------------------------------- workloads
NIFT_SRC = {
    "fib22_top": "fn(fib(n)) { if(n < 2) { return n } return fib(n - 1) + fib(n - 2) }\nprint(fib(22))\n",
    "fib22_loop": "fn(fib(n)) { if(n < 2) { return n } return fib(n - 1) + fib(n - 2) }\n"
                  "i := 0\nwhile(i < 1) { print(fib(22)); i += 1 }\n",
    "loop1m": "i := 0\ns := 0\nwhile(i < 1000000) { s += i; i += 1 }\nprint(s)\n",
    "calls200k": "fn(add(a, b)) { return a + b }\ni := 0\ns := 0\n"
                 "while(i < 200000) { s = add(s, i); i += 1 }\nprint(s)\n",
    "sortkey20k": "a := []\ni := 0\nwhile(i < 20000) { a.push((i * 7919) % 20000); i += 1 }\n"
                  "b := a.sort_by(x => x)\nprint(b[19999])\n",
    "strbuild50k": "t := \"\"\ni := 0\nwhile(i < 50000) { t = t + \"x\"; i += 1 }\nprint(t.length())\n",
    "argcopy100k": "fn(len(x)) { return x.length() }\na := []\ni := 0\nwhile(i < 100000) { a.push(i); i += 1 }\n"
                   "j := 0\nt := 0\nwhile(j < 200) { t += len(a); j += 1 }\nprint(t)\n",
}
PY_SRC = {
    "fib22_top": "def fib(n): return n if n < 2 else fib(n-1) + fib(n-2)\nprint(fib(22))\n",
    "loop1m": "def main():\n  i=0\n  s=0\n  while i < 1000000: s += i; i += 1\n  print(s)\nmain()\n",
    "calls200k": "def add(a,b): return a+b\ndef main():\n  i=0\n  s=0\n  while i < 200000:\n    s = add(s,i); i += 1\n"
                 "  print(s)\nmain()\n",
    "sortkey20k": "a=[(i*7919)%20000 for i in range(20000)]\nb=sorted(a,key=lambda x:x)\nprint(b[19999])\n",
    "strbuild50k": "def main():\n  t=''\n  for i in range(50000): t = t + 'x'\n  print(len(t))\nmain()\n",
}
GOLDEN = {"fib22_top": "17711", "fib22_loop": "17711", "loop1m": "499999500000", "calls200k": "19999900000",
          "sortkey20k": "19999", "strbuild50k": "50000", "argcopy100k": "20000000"}

OPS = {  # name: (nift template, python function-local template); {N} = iterations
    "loop": ("i := 0\nwhile(i < {N}) {{ i += 1 }}\nprint(i)\n", "i=0\nwhile i < {N}: i += 1\nprint(i)"),
    "add_local": ("i := 0\ns := 0\nwhile(i < {N}) {{ s += i; i += 1 }}\nprint(s)\n",
                  "i=0\ns=0\nwhile i < {N}:\n  s += i; i += 1\nprint(s)"),
    "call": ("fn(add(a, b)) {{ return a + b }}\ni := 0\ns := 0\nwhile(i < {N}) {{ s = add(s, i); i += 1 }}\nprint(s)\n",
             "i=0\ns=0\nwhile i < {N}:\n  s = add(s, i); i += 1\nprint(s)"),
    "array_index": ("a := [1,2,3,4,5,6,7,8]\ni := 0\ns := 0\nwhile(i < {N}) {{ s += a[3]; i += 1 }}\nprint(s)\n",
                    "a=[1,2,3,4,5,6,7,8]\ni=0\ns=0\nwhile i < {N}:\n  s += a[3]; i += 1\nprint(s)"),
    "array_push": ("a := []\ni := 0\nwhile(i < {N}) {{ a.push(i); i += 1 }}\nprint(a.length())\n",
                   "a=[]\ni=0\nwhile i < {N}:\n  a.append(i); i += 1\nprint(len(a))"),
    "object_member": ("o := {{\"alpha\": 1, \"beta\": 2, \"gamma\": 3, \"delta\": 4}}\ni := 0\ns := 0\n"
                      "while(i < {N}) {{ s += o.delta; i += 1 }}\nprint(s)\n",
                      "o={{'alpha':1,'beta':2,'gamma':3,'delta':4}}\ni=0\ns=0\nwhile i < {N}:\n  s += o['delta']; i += 1\nprint(s)"),
    "string_concat": ("t := \"\"\ni := 0\nwhile(i < {N}) {{ t = t + \"x\"; i += 1 }}\nprint(t.length())\n",
                      "t=''\ni=0\nwhile i < {N}:\n  t = t + 'x'; i += 1\nprint(len(t))"),
    "if_branch": ("i := 0\ns := 0\nwhile(i < {N}) {{ if(i % 2 == 0) {{ s += 1 }} i += 1 }}\nprint(s)\n",
                  "i=0\ns=0\nwhile i < {N}:\n  if i % 2 == 0: s += 1\n  i += 1\nprint(s)"),
}
FIB_CALLS = {10: 177, 12: 465}


def py_fn(body, name):
    lines = body.split("\n")
    pre = "def add(a,b): return a+b\n" if name == "call" else ""
    return pre + "def main():\n" + "\n".join("  " + l for l in lines) + "\nmain()\n"


def write(d, name, text):
    p = os.path.join(d, name)
    with open(p, "w") as f:
        f.write(text)
    return p


# ---------------------------------------------------------------- groups
def g_startup(a, d, emit):
    emit("startup", "nift_version", median_wall([a.nift, "version"], a.runs * 4))
    emit("startup", "nift_empty_script", median_wall([a.nift, write(d, "empty.f", "x := 1\n")], a.runs * 4))
    emit("startup", "python3_pass", median_wall(["python3", "-c", "pass"], a.runs * 4))
    emit("startup", "bash_true", median_wall(["bash", "-c", "true"], a.runs * 4))


def g_kernels(a, d, emit):
    for k, src in NIFT_SRC.items():
        emit("kernels", f"nift.{k}", median_wall([a.nift, write(d, k + ".f", src)], a.runs, GOLDEN[k]))
    for k, src in PY_SRC.items():
        emit("kernels", f"python3.{k}", median_wall(["python3", "-I", write(d, k + ".py", src)], a.runs, GOLDEN[k]))


def g_ops(a, d, emit):
    n1, n2 = 4000, 8000
    for name, (nt, pt) in OPS.items():
        nift_ir = []
        py_ir = []
        for n in (n1, n2):
            ir, _ = callgrind_ir([a.nift, write(d, f"op_{name}.f", nt.format(N=n))])
            nift_ir.append(ir)
            ir, _ = callgrind_ir(["python3", "-I", write(d, f"op_{name}.py", py_fn(pt.format(N=n), name))])
            py_ir.append(ir)
        nv = (nift_ir[1] - nift_ir[0]) / (n2 - n1)
        pv = (py_ir[1] - py_ir[0]) / (n2 - n1)
        emit("ops", name, {"nift_instr": round(nv), "cpython_fn_instr": round(pv), "ratio": round(nv / pv, 2)})
    # recursive call cost, top-level vs inside a loop
    for label, wrap in (("fib_call_toplevel", False), ("fib_call_in_loop", True)):
        vals = []
        for n in (10, 12):
            body = "fn(fib(n)) { if(n < 2) { return n } return fib(n - 1) + fib(n - 2) }\n"
            body += (f"i := 0\nwhile(i < 1) {{ print(fib({n})); i += 1 }}\n" if wrap else f"print(fib({n}))\n")
            vals.append(callgrind_ir([a.nift, write(d, "opfib.f", body)])[0])
        emit("ops", label, {"nift_instr_per_call": round((vals[1] - vals[0]) / (FIB_CALLS[12] - FIB_CALLS[10]))})
    vals = []
    for n in (10, 12):
        vals.append(callgrind_ir(["python3", "-I", write(d, "opfib.py",
                    f"def fib(n): return n if n<2 else fib(n-1)+fib(n-2)\nprint(fib({n}))\n")])[0])
    emit("ops", "fib_call_cpython", {"cpython_instr_per_call": round((vals[1] - vals[0]) / (FIB_CALLS[12] - FIB_CALLS[10]))})


def g_process(a, d, emit):
    nf = write(d, "spawn.f", "i := 0\nwhile(i < 300) { r := cmd(\"/bin/true\").run(); i += 1 }\nprint(i)\n")
    emit("process", "nift_cmd_run_x300", median_wall([a.nift, nf], a.runs, "300"))
    emit("process", "bash_direct_x300", median_wall(
        ["bash", write(d, "spawn.sh", "for ((i=0;i<300;i++)); do /bin/true; done; echo $i\n")], a.runs, "300"))
    emit("process", "bash_capture_x300", median_wall(
        ["bash", write(d, "spawnc.sh", "for ((i=0;i<300;i++)); do x=$(/bin/true); done; echo $i\n")], a.runs, "300"))
    emit("process", "python_subprocess_x300", median_wall(["python3", "-I", write(d, "spawn.py",
         "import subprocess\nfor i in range(300): subprocess.run(['/bin/true'], capture_output=True)\nprint(300)\n")],
         a.runs, "300"))


def g_async(a, d, emit):
    for n in (1000, 2000, 4000):
        src = ("fn[async](work(x)) { return x + 1 }\nfs := []\ni := 0\n"
               f"while(i < {n}) {{ fs.push(work(i)); i += 1 }}\nt := 0\nj := 0\n"
               f"while(j < {n}) {{ t += await fs[j]; j += 1 }}\nprint(t)\n")
        emit("async", f"futures_{n}", median_wall([a.nift, write(d, f"as{n}.f", src)], max(1, a.runs // 2),
             str(n * (n + 1) // 2)))


def make_site(root, pages):
    import random
    words = "alpha beta gamma delta build graph cache render template incremental node page value site".split()
    os.makedirs(os.path.join(root, ".nift"))
    for sub in ("content", "templates", "public"):
        os.makedirs(os.path.join(root, sub))
    json.dump({"config": {"content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
               "output-ext": ".html", "default-template": "templates/template.html", "build-threads": -1,
               "incremental-mode": "modified", "minify-exts": []}}, open(os.path.join(root, ".nift/config.json"), "w"))
    json.dump({"tracked": [{"name": f"p{i}", "title": f"Page {i}", "template": "templates/template.html"}
               for i in range(pages)]}, open(os.path.join(root, ".nift/tracked.json"), "w"))
    write(root, "templates/head.html", '<meta charset="utf-8">\n<title>$[title]</title>\n')
    write(root, "templates/nav.html", '<nav><a href="/">Home</a></nav>\n')
    write(root, "templates/template.html", '<!doctype html>\n<html lang="en">\n<head>\n@input("templates/head.html")\n'
          '</head>\n<body>\n@input("templates/nav.html")\n<h1>$[title]</h1>\n<main>\n@content\n</main>\n</body>\n</html>\n')
    for i in range(pages):
        r = random.Random(i)
        para = lambda k: " ".join(r.choice(words) for _ in range(k))
        body = (f"## Section {i}\n\n{para(60)} *emph* and **strong** with `code_{i}`.\n\n- {para(8)}\n- {para(8)}\n\n"
                f"See [next](p{(i + 1) % pages}.html).\n\n### Details\n\n{para(120)}\n")
        write(root, f"content/p{i}.html", '@markup("md") {\n' + body + '}\n')


def g_site(a, d, emit):
    root = os.path.join(d, "site2k")
    make_site(root, 2000)
    run([a.nift, "build", "--all"], root)
    emit("site", "full_2k", median_wall([a.nift, "build", "--all"], a.runs, cwd=root))
    emit("site", "noop_2k", median_wall([a.nift, "build"], a.runs, cwd=root))
    p = os.path.join(root, "content/p123.html")
    open(p, "a").write("\n")
    dt, _ = run([a.nift, "build"], root)
    emit("site", "one_page_2k", {"wall_s": round(dt, 5)})
    p = os.path.join(root, "templates/nav.html")
    open(p, "a").write("<!-- x -->\n")
    dt, _ = run([a.nift, "build"], root)
    emit("site", "shared_partial_2k", {"wall_s": round(dt, 5)})
    if shutil.which("valgrind"):
        small = os.path.join(d, "site300")
        make_site(small, 300)
        cfg = os.path.join(small, ".nift/config.json")
        c = json.load(open(cfg))
        c["config"]["build-threads"] = 1
        json.dump(c, open(cfg, "w"))
        ir, _ = callgrind_ir([a.nift, "build", "--all"], small)
        emit("site", "profile_300_instr_per_page", {"instr_per_page": round(ir / 300)})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--groups", default=",".join(GROUPS))
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    a.nift = os.path.abspath(a.nift)
    ver = subprocess.run([a.nift, "version"], stdout=subprocess.PIPE, text=True).stdout.strip()
    out = open(a.out, "w") if a.out else sys.stdout
    meta = {"kind": "meta", "nift": ver, "binary": a.nift, "host": host()}
    out.write(json.dumps(meta) + "\n")

    def emit(group, name, data):
        rec = {"group": group, "name": name, **data}
        out.write(json.dumps(rec) + "\n")
        out.flush()
        if out is not sys.stdout:
            print(json.dumps(rec), flush=True)

    with tempfile.TemporaryDirectory(prefix="nift-rt2-") as d:
        for g in a.groups.split(","):
            globals()["g_" + g](a, d, emit)


if __name__ == "__main__":
    main()
