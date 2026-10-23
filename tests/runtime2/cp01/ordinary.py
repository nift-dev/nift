#!/usr/bin/env python3
"""Ordinary-build correctness scenarios against real nift binaries.

usage: ordinary.py label=/path/to/nift ...

Every edit in the ORDINARY scenarios is made after sleeping past a timestamp
tick (50 ms), and the harness checks that the edited file's mtime is strictly
greater than every existing page-info mtime ("distinguishable metadata").
Oracle: after every build, public/ must equal a clean `build --all` of a copy
of the same sources made with the same binary; and `nift status` / a following
`nift build` must agree (no stale page reported up to date).
Same-tick / sub-tick cases are reported separately (IMMEDIATE) and are outside
the owner's modified-mode contract except where noted.
"""
import json, os, shutil, signal, subprocess, sys, tempfile, time

TICK = 0.05
BASE = tempfile.mkdtemp(prefix="cp01-ordinary-")

def run(nift, cwd, *args, env=None, timeout=600):
    e = dict(os.environ); e.update(env or {})
    p = subprocess.run([nift, *args], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=e, timeout=timeout)
    return p.returncode, p.stdout

import re
def built(out):
    """Names of rebuilt pages when listed individually; otherwise a list of
    N placeholders from the '📦 N files (re)built' summary (count-only)."""
    names = sorted(l.split(" ", 1)[1].strip() for l in out.splitlines() if l.startswith("built "))
    if names: return names
    m = re.search(r"(\d+) files? (?:re)?built", out)
    return ["?"] * int(m.group(1)) if m else []

def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f: f.write(text)

def mksite(root, n, mode="modified", threads=1, pad=0):
    for d in (".nift", "content", "templates", "public"): os.makedirs(os.path.join(root, d), exist_ok=True)
    json.dump({"config": {"content-dir": "content/", "content-ext": ".html", "output-dir": "public/", "output-ext": ".html",
               "default-template": "templates/template.html", "build-threads": threads, "incremental-mode": mode, "minify-exts": []}},
              open(f"{root}/.nift/config.json", "w"))
    tracked = [{"name": f"p{i}", "title": f"P{i}", "template": "templates/template.html" if i >= 10 else "templates/t2.html"} for i in range(n)]
    tracked.append({"name": "index", "title": "Index", "template": "templates/template.html"})
    json.dump({"tracked": tracked}, open(f"{root}/.nift/tracked.json", "w"))
    write(f"{root}/templates/head.html", "<title>$[title]</title>\n")
    write(f"{root}/templates/template.html", '<html><head>@input("templates/head.html")</head><body>TPL1 @content</body></html>\n')
    write(f"{root}/templates/t2.html", '<html><head>@input("templates/head.html")</head><body>T2 @input("templates/part.html") @content</body></html>\n')
    write(f"{root}/templates/part.html", "PART1\n")
    for i in range(n): write(f"{root}/content/p{i}.html", f"<p>page {i}</p>\n" + "x" * pad)
    write(f"{root}/content/index.html", 'INDEX @input("content/p1.html")\n')

def set_mode(root, mode):
    c = json.load(open(f"{root}/.nift/config.json")); c["config"]["incremental-mode"] = mode
    json.dump(c, open(f"{root}/.nift/config.json", "w"))

def tree(d):
    out = {}
    for dp, _, fs in os.walk(d):
        for f in fs:
            p = os.path.join(dp, f); out[os.path.relpath(p, d)] = open(p, "rb").read()
    return out

def clean_oracle(nift, root):
    t = tempfile.mkdtemp(dir=BASE)
    for d in ("content", "templates"): shutil.copytree(f"{root}/{d}", f"{t}/{d}")
    os.makedirs(f"{t}/.nift"); os.makedirs(f"{t}/public")
    for f in ("config.json", "tracked.json"): shutil.copy(f"{root}/.nift/{f}", f"{t}/.nift/{f}")
    rc, out = run(nift, t, "build", "--all")
    r = tree(f"{t}/public"); shutil.rmtree(t); return r

def pageinfo_max_mtime(root):
    m = 0
    for dp, dn, fs in os.walk(f"{root}/.nift"):
        if "clock" in dn: dn.remove("clock")
        for f in fs:
            if f.endswith(".info.json"): m = max(m, os.stat(os.path.join(dp, f)).st_mtime_ns)
    return m

class Ctx:
    def __init__(self, label, nift):
        self.label, self.nift, self.fails, self.notes = label, nift, [], []
    def check(self, root, what, expect_built=None, out=None):
        ok = tree(f"{root}/public") == clean_oracle(self.nift, root)
        b = built(out) if out is not None else None
        if not ok: self.fails.append(f"{what}: output differs from clean build")
        if expect_built is not None and b is not None and (sorted(expect_built) != b if b[:1] != ["?"] else len(expect_built) != len(b)):
            self.fails.append(f"{what}: rebuilt {b} expected {sorted(expect_built)}")
        rc, st = run(self.nift, root, "status")
        honest = ("up to date" in st) == ok
        if ok and "up to date" not in st: self.notes.append(f"{what}: status not clean after correct build (extra rebuild pending): {st.strip().splitlines()[-2:] }")
        if not ok and "up to date" in st: self.fails.append(f"{what}: STALE and status reports up to date")
        return ok
    def edit(self, root, rel, text, append=False):
        time.sleep(TICK)
        if append: open(f"{root}/{rel}", "a").write(text)
        else: write(f"{root}/{rel}", text)
        if os.stat(f"{root}/{rel}").st_mtime_ns <= pageinfo_max_mtime(root):
            self.fails.append(f"harness: edit of {rel} not strictly newer than page-info (not an ordinary change)")

def scen_basic(c):
    r = tempfile.mkdtemp(dir=BASE); mksite(r, 30)
    rc, out = run(c.nift, r, "build", "--all"); c.check(r, "S0 initial", None, out)
    time.sleep(TICK)
    # S5 repeated no-op builds, nothing rewritten
    snap = {p: os.stat(os.path.join(dp, p)).st_mtime_ns for dp, _, fs in os.walk(r) for p in fs if not dp.endswith("/.nift") or True}
    before = {os.path.join(dp, f): os.stat(os.path.join(dp, f)).st_mtime_ns for dp, _, fs in os.walk(r) for f in fs}
    nb = [len(built(run(c.nift, r, "build")[1])) for _ in range(10)]
    after = {os.path.join(dp, f): os.stat(os.path.join(dp, f)).st_mtime_ns for dp, _, fs in os.walk(r) for f in fs}
    changed = [k for k in after if before.get(k) != after[k] and not k.endswith(".lock")]
    c.notes.append(f"S5 rebuilds per no-op build: {nb}; files rewritten: {len(changed)}")
    if any(nb) or changed: c.fails.append(f"S5 no-op not a no-op: {nb} {changed[:3]}")
    # S1 one source edit
    c.edit(r, "content/p12.html", "<p>page 12 EDIT</p>\n")
    rc, out = run(c.nift, r, "build"); c.check(r, "S1 one content edit", ["p12"], out)
    # S2 shared template edit (template.html: p10..p29 + index)
    c.edit(r, "templates/template.html", '<html><head>@input("templates/head.html")</head><body>TPL2 @content</body></html>\n')
    rc, out = run(c.nift, r, "build"); c.check(r, "S2 shared template edit", [f"p{i}" for i in range(10, 30)] + ["index"], out)
    # S2b shared partial used by both templates
    c.edit(r, "templates/head.html", "<title>H2 $[title]</title>\n")
    rc, out = run(c.nift, r, "build"); c.check(r, "S2b shared partial (head) edit", [f"p{i}" for i in range(30)] + ["index"], out)
    # S3 multiple dependent pages
    c.edit(r, "templates/part.html", "PART2\n")
    rc, out = run(c.nift, r, "build"); c.check(r, "S3a partial used by p0..p9", [f"p{i}" for i in range(10)], out)
    c.edit(r, "content/p1.html", "<p>page 1 EDIT</p>\n")
    rc, out = run(c.nift, r, "build"); c.check(r, "S3b content consumed by two pages", ["p1", "index"], out)
    # S4 dependency addition and removal
    c.edit(r, "templates/new.html", "NEW1\n")
    rc, out = run(c.nift, r, "build"); c.check(r, "S4a create unreferenced file", [], out)
    c.edit(r, "templates/t2.html", '<html><head>@input("templates/head.html")</head><body>T2 @input("templates/part.html") @input("templates/new.html") @content</body></html>\n')
    rc, out = run(c.nift, r, "build"); c.check(r, "S4b add dependency", [f"p{i}" for i in range(10)], out)
    c.edit(r, "templates/new.html", "NEW2\n")
    rc, out = run(c.nift, r, "build"); c.check(r, "S4c edit added dependency", [f"p{i}" for i in range(10)], out)
    c.edit(r, "templates/t2.html", '<html><head>@input("templates/head.html")</head><body>T2 @input("templates/part.html") @content</body></html>\n')
    rc, out = run(c.nift, r, "build"); c.check(r, "S4d remove dependency", [f"p{i}" for i in range(10)], out)
    c.edit(r, "templates/new.html", "NEW3\n")
    rc, out = run(c.nift, r, "build"); c.check(r, "S4e edit removed dependency (no rebuild)", [], out)
    time.sleep(TICK); os.remove(f"{r}/templates/new.html")
    rc, out = run(c.nift, r, "build"); c.check(r, "S4f delete removed dependency (no rebuild)", [], out)
    # S6 targeted one-page and full rebuilds
    c.edit(r, "content/p20.html", "<p>p20 E</p>\n"); c.edit(r, "content/p21.html", "<p>p21 E</p>\n")
    rc, out = run(c.nift, r, "build", "p20")
    if built(out) != ["p20"] and "specified files built successfully" not in out:
        c.fails.append(f"S6a targeted build output was neither p20 nor the targeted summary: {out[-200:]}")
    rc, out = run(c.nift, r, "build"); c.check(r, "S6b remaining edit after targeted build", ["p21"], out)
    rc, out = run(c.nift, r, "build", "--all"); c.check(r, "S6c build --all", [f"p{i}" for i in range(30)] + ["index"], out)
    time.sleep(TICK)
    nb = [len(built(run(c.nift, r, "build")[1])) for _ in range(5)]
    if any(nb): c.fails.append(f"S6d no-op after --all rebuilt {nb}")
    # S8 mode switching modified -> hash -> modified, with edits
    set_mode(r, "hash"); rc, out = run(c.nift, r, "build"); c.check(r, "S8a switch to hash", None, out)
    c.notes.append(f"S8a switch modified->hash rebuilt {len(built(out))}")
    c.edit(r, "content/p3.html", "<p>p3 hash edit</p>\n")
    rc, out = run(c.nift, r, "build"); c.check(r, "S8b edit in hash mode", ["p3"], out)
    set_mode(r, "modified"); time.sleep(TICK); rc, out = run(c.nift, r, "build"); c.check(r, "S8c switch back to modified", None, out)
    c.notes.append(f"S8c switch hash->modified rebuilt {len(built(out))}")
    c.edit(r, "templates/part.html", "PART3\n")
    rc, out = run(c.nift, r, "build"); c.check(r, "S8d edit after switching back", [f"p{i}" for i in range(10)], out)
    time.sleep(TICK)
    nb = [len(built(run(c.nift, r, "build")[1])) for _ in range(5)]
    if any(nb): c.fails.append(f"S8e no-op after mode switches rebuilt {nb}")

def scen_interrupt(c, delays):
    for d in delays:
        r = tempfile.mkdtemp(dir=BASE); mksite(r, 1500, pad=3000)
        run(c.nift, r, "build", "--all")
        c.edit(r, "templates/template.html", '<html><head>@input("templates/head.html")</head><body>TPL-K @content</body></html>\n')
        p = subprocess.Popen([c.nift, "build"], cwd=r, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(d); killed = p.poll() is None
        if killed: os.kill(p.pid, signal.SIGKILL)
        p.wait()
        state = "killed" if killed else "finished-before-kill"
        stale_now = sum(1 for f in os.listdir(f"{r}/public") if "TPL-K" not in open(f"{r}/public/{f}").read() and f.startswith("p") and int(f[1:-5]) >= 10)
        rc, out = run(c.nift, r, "build"); nb1 = len(built(out)); first = out.strip().splitlines()[:2]
        ok1 = tree(f"{r}/public") == clean_oracle(c.nift, r)
        if not ok1 and rc != 0:
            rc2, out2 = run(c.nift, r, "build", "--repair"); c.notes.append(f"K{d}: plain build rc={rc} ({first}); needed --repair rc={rc2}")
        ok = c.check(r, f"K kill@{d}s ({state}, {stale_now} stale outputs before recovery) -> next build", None, None)
        nb = [len(built(run(c.nift, r, "build")[1])) for _ in range(3)]
        c.notes.append(f"K kill@{d}s {state}: recovery build rc={rc} rebuilt {nb1}, then {nb}; ok_after_plain_build={ok1}")
        shutil.rmtree(r)

def scen_crashpoints(c):
    # NIFT_TEST_CRASH_AT points exist in Runtime 2.0 binaries only.
    for point in ("page-rendered:p12", "page-output:p12", "page-info:p12", "page-post:p12", "page-done:p12"):
        r = tempfile.mkdtemp(dir=BASE); mksite(r, 30)
        run(c.nift, r, "build", "--all")
        c.edit(r, "templates/template.html", '<html><head>@input("templates/head.html")</head><body>TPL-C @content</body></html>\n')
        rc, out = run(c.nift, r, "build", env={"NIFT_TEST_CRASH_AT": point})
        rc2, out2 = run(c.nift, r, "build")
        c.check(r, f"C crash at {point} (rc={rc}) -> next build", None, None)
        nb = [len(built(run(c.nift, r, "build")[1])) for _ in range(3)]
        c.notes.append(f"C {point}: crash rc={rc}, recovery rebuilt {len(built(out2))}, then {nb}")
        shutil.rmtree(r)

def scen_immediate(c, cycles):
    # IMMEDIATE (outside "ordinary": no sleep). Edit right AFTER a build returns.
    r = tempfile.mkdtemp(dir=BASE); mksite(r, 3)
    run(c.nift, r, "build", "--all")
    miss = persistent = 0
    for i in range(cycles):
        write(f"{r}/content/p12.html" if False else f"{r}/content/p1.html", f"MARK-{i}\n")
        run(c.nift, r, "build")
        if f"MARK-{i}" not in open(f"{r}/public/p1.html").read():
            miss += 1
            for _ in range(3): run(c.nift, r, "build")
            if f"MARK-{i}" not in open(f"{r}/public/p1.html").read():
                persistent += 1
                time.sleep(TICK); write(f"{r}/content/p1.html", f"REPAIR-{i}\n"); run(c.nift, r, "build")
    c.notes.append(f"I immediate edit->build {cycles} cycles: missed {miss}, persistent after 3 more builds {persistent}")
    if persistent: c.fails.append(f"I immediate edits: {persistent}/{cycles} PERSISTENTLY stale (edit made after the previous build returned)")

if __name__ == "__main__":
    which = os.environ.get("ONLY", "basic,interrupt,crash,immediate").split(",")
    failed = 0
    for spec in sys.argv[1:]:
        label, nift = spec.split("=", 1)
        c = Ctx(label, nift)
        if "basic" in which: scen_basic(c)
        if "interrupt" in which: scen_interrupt(c, [0.05, 0.12, 0.25])
        if "crash" in which and label != "v412": scen_crashpoints(c)
        if "immediate" in which: scen_immediate(c, int(os.environ.get("CYCLES", "300")))
        print(f"=== {label} ({nift})\nFAILS ({len(c.fails)}):")
        for f in c.fails: print("  FAIL", f)
        for n in c.notes: print("  note", n)
        sys.stdout.flush()
        if c.fails: failed = 1
    shutil.rmtree(BASE, ignore_errors=True)
    sys.exit(failed)
