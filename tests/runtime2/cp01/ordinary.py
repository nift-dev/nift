#!/usr/bin/env python3
"""Ordinary-build correctness scenarios against real nift binaries.

usage: ordinary.py label=/path/to/nift ...

Every edit in the ORDINARY scenarios is made after ageing the whole site (every
file moved 10 s into the past, order kept), and the harness checks that the
edited file's mtime is strictly greater than every existing page-info mtime
("distinguishable metadata"). No scenario waits for the clock.
Oracle: after every build, public/ must equal a clean `build --all` of a copy
of the same sources made with the same binary; and `nift status` / a following
`nift build` must agree (no stale page reported up to date).
Same-tick / sub-tick cases are reported separately (IMMEDIATE) and are outside
the owner's modified-mode contract except where noted.

Edits are sequenced (CP01 flakiness fix, 2026-11-08): ageing the site before an
edit only separates it from the PREVIOUS build (it replaced a 50 ms sleep). The build that follows an edit
can finish inside the filesystem's coarse timestamp tick (4 ms here; with
multigrain timestamps its page-info then gets exactly the edit's mtime), and the
v4.12 rule (rebuild when a dependency is not older than the page-info) then
rebuilds that page once more: an extra rebuild the scenario did not intend. So
after writing, edit() sequences it (ticks.py: the edited file's mtime becomes
T - 1 ns, T the filesystem clock now): every later write, including the next
build's page-info, is stamped at least T (the clock never goes backwards), so
the edit is strictly older than the build, whatever the scheduling. CP01_EDIT=unsequenced restores the old edit (for the regression
test); CP01_WORST_CASE_TICK=1 stamps every page-info a build writes with the
earliest legal time (the edit's own mtime when unsequenced, T when sequenced),
i.e. the fastest possible build, deterministically.
"""
import json, os, shutil, signal, subprocess, sys, tempfile, time

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

def sequence_site(root):
    """The generated inputs, stamped just before the clock: older than the first build's writes."""
    files = [os.path.join(dp, f) for dp, _, fs in os.walk(root) for f in fs]
    if files:
        ticks.sequence(files)

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
    sequence_site(root)

def set_mode(root, mode):
    c = json.load(open(f"{root}/.nift/config.json")); c["config"]["incremental-mode"] = mode
    json.dump(c, open(f"{root}/.nift/config.json", "w"))

def tree(d):
    out = {}
    for dp, _, fs in os.walk(d):
        for f in fs:
            p = os.path.join(dp, f); out[os.path.relpath(p, d)] = open(p, "rb").read()
    return out

def clean_oracle(nift, root, errors=None):
    t = tempfile.mkdtemp(dir=BASE)
    for d in ("content", "templates"): shutil.copytree(f"{root}/{d}", f"{t}/{d}")
    os.makedirs(f"{t}/.nift"); os.makedirs(f"{t}/public")
    for f in ("config.json", "tracked.json"): shutil.copy(f"{root}/.nift/{f}", f"{t}/.nift/{f}")
    rc, out = run(nift, t, "build", "--all")
    if rc != 0:
        if errors is not None: errors.append(f"clean-oracle build failed rc={rc}: {out.strip().splitlines()[:2]}")
        r = {}
    else:
        r = tree(f"{t}/public")
    shutil.rmtree(t); return r

def pageinfo_max_mtime(root):
    m = 0
    for dp, dn, fs in os.walk(f"{root}/.nift"):
        if "clock" in dn: dn.remove("clock")
        for f in fs:
            if f.endswith(".info.json"): m = max(m, os.stat(os.path.join(dp, f)).st_mtime_ns)
    return m

sys.dont_write_bytecode = True  # no __pycache__ in the tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ticks  # noqa: E402  (the shared sequencing helper; settle.sh uses it too)

def pageinfo_mtimes(root):
    out = {}
    for dp, dn, fs in os.walk(f"{root}/.nift"):
        for f in fs:
            if f.endswith(".info.json"): out[os.path.join(dp, f)] = os.stat(os.path.join(dp, f)).st_mtime_ns
    return out

class Ctx:
    def __init__(self, label, nift):
        self.label, self.nift, self.fails, self.notes = label, nift, [], []
        self.sequenced = os.environ.get("CP01_EDIT", "sequenced") != "unsequenced"
        self.worst = os.environ.get("CP01_WORST_CASE_TICK") == "1"
        self.floor = None  # the earliest time the next build's writes can carry
    def build(self, root, *args, env=None):
        before = pageinfo_mtimes(root) if self.worst else None
        rc, out = run(self.nift, root, *args, env=env)
        if self.worst and self.floor is not None:
            for f, m in pageinfo_mtimes(root).items():
                if before.get(f) != m:
                    os.utime(f, ns=(os.stat(f).st_atime_ns, self.floor))
        self.floor = None
        return rc, out
    def check(self, root, what, expect_built=None, out=None, rc=0):
        want = clean_oracle(self.nift, root, self.fails)
        ok = tree(f"{root}/public") == want
        if rc != 0:
            self.fails.append(f"{what}: build failed rc={rc}")
        b = built(out) if out is not None else None
        if not ok: self.fails.append(f"{what}: output differs from clean build")
        if expect_built is not None and b is not None and (sorted(expect_built) != b if b[:1] != ["?"] else len(expect_built) != len(b)):
            self.fails.append(f"{what}: rebuilt {b} expected {sorted(expect_built)}")
        rc, st = run(self.nift, root, "status")  # status writes no page-info
        if rc != 0: self.fails.append(f"{what}: status failed rc={rc}")
        honest = ("up to date" in st) == ok
        if ok and "up to date" not in st: self.notes.append(f"{what}: status not clean after correct build (extra rebuild pending): {st.strip().splitlines()[-2:] }")
        if not ok and "up to date" in st: self.fails.append(f"{what}: STALE and status reports up to date")
        return ok
    def built_count(self, root, label):
        rc, out = self.build(root, "build")
        if rc != 0: self.fails.append(f"{label}: build failed rc={rc}")
        return len(built(out))

    def edit(self, root, rel, text, append=False):
        # review of a977376e: no sleep. The whole site is aged first (every file moved 10 s into
        # the past, order kept), so the edit is newer than everything the previous build wrote
        # without waiting for the clock; then it is sequenced before the next build.
        ticks.age(root)
        if append: open(f"{root}/{rel}", "a").write(text)
        else: write(f"{root}/{rel}", text)
        path = f"{root}/{rel}"
        if self.sequenced:
            self.floor = ticks.sequence([path])
        else:
            self.floor = os.stat(path).st_mtime_ns
        if os.stat(path).st_mtime_ns <= pageinfo_max_mtime(root):
            self.fails.append(f"harness: edit of {rel} not strictly newer than page-info (not an ordinary change)")

def scen_basic(c):
    r = tempfile.mkdtemp(dir=BASE); mksite(r, 30)
    rc, out = c.build(r, "build", "--all"); c.check(r, "S0 initial", None, out, rc)
    # S5 repeated no-op builds, nothing rewritten
    snap = {p: os.stat(os.path.join(dp, p)).st_mtime_ns for dp, _, fs in os.walk(r) for p in fs if not dp.endswith("/.nift") or True}
    before = {os.path.join(dp, f): os.stat(os.path.join(dp, f)).st_mtime_ns for dp, _, fs in os.walk(r) for f in fs}
    nb = [c.built_count(r, "S5 no-op") for _ in range(10)]
    after = {os.path.join(dp, f): os.stat(os.path.join(dp, f)).st_mtime_ns for dp, _, fs in os.walk(r) for f in fs}
    changed = [k for k in after if before.get(k) != after[k] and not k.endswith(".lock")]
    c.notes.append(f"S5 rebuilds per no-op build: {nb}; files rewritten: {len(changed)}")
    if any(nb) or changed: c.fails.append(f"S5 no-op not a no-op: {nb} {changed[:3]}")
    # S1 one source edit
    c.edit(r, "content/p12.html", "<p>page 12 EDIT</p>\n")
    rc, out = c.build(r, "build"); c.check(r, "S1 one content edit", ["p12"], out, rc)
    # S2 shared template edit (template.html: p10..p29 + index)
    c.edit(r, "templates/template.html", '<html><head>@input("templates/head.html")</head><body>TPL2 @content</body></html>\n')
    rc, out = c.build(r, "build"); c.check(r, "S2 shared template edit", [f"p{i}" for i in range(10, 30)] + ["index"], out, rc)
    # S2b shared partial used by both templates
    c.edit(r, "templates/head.html", "<title>H2 $[title]</title>\n")
    rc, out = c.build(r, "build"); c.check(r, "S2b shared partial (head) edit", [f"p{i}" for i in range(30)] + ["index"], out, rc)
    # S3 multiple dependent pages
    c.edit(r, "templates/part.html", "PART2\n")
    rc, out = c.build(r, "build"); c.check(r, "S3a partial used by p0..p9", [f"p{i}" for i in range(10)], out, rc)
    c.edit(r, "content/p1.html", "<p>page 1 EDIT</p>\n")
    rc, out = c.build(r, "build"); c.check(r, "S3b content consumed by two pages", ["p1", "index"], out, rc)
    # S4 dependency addition and removal
    c.edit(r, "templates/new.html", "NEW1\n")
    rc, out = c.build(r, "build"); c.check(r, "S4a create unreferenced file", [], out, rc)
    c.edit(r, "templates/t2.html", '<html><head>@input("templates/head.html")</head><body>T2 @input("templates/part.html") @input("templates/new.html") @content</body></html>\n')
    rc, out = c.build(r, "build"); c.check(r, "S4b add dependency", [f"p{i}" for i in range(10)], out, rc)
    c.edit(r, "templates/new.html", "NEW2\n")
    rc, out = c.build(r, "build"); c.check(r, "S4c edit added dependency", [f"p{i}" for i in range(10)], out, rc)
    c.edit(r, "templates/t2.html", '<html><head>@input("templates/head.html")</head><body>T2 @input("templates/part.html") @content</body></html>\n')
    rc, out = c.build(r, "build"); c.check(r, "S4d remove dependency", [f"p{i}" for i in range(10)], out, rc)
    c.edit(r, "templates/new.html", "NEW3\n")
    rc, out = c.build(r, "build"); c.check(r, "S4e edit removed dependency (no rebuild)", [], out, rc)
    os.remove(f"{r}/templates/new.html")
    rc, out = c.build(r, "build"); c.check(r, "S4f delete removed dependency (no rebuild)", [], out, rc)
    # S6 targeted one-page and full rebuilds
    c.edit(r, "content/p20.html", "<p>p20 E</p>\n"); c.edit(r, "content/p21.html", "<p>p21 E</p>\n")
    rc, out = c.build(r, "build", "p20")
    if rc != 0: c.fails.append(f"S6a targeted build failed rc={rc}")
    if built(out) != ["p20"] and "specified files built successfully" not in out:
        c.fails.append(f"S6a targeted build output was neither p20 nor the targeted summary: {out[-200:]}")
    rc, out = c.build(r, "build"); c.check(r, "S6b remaining edit after targeted build", ["p21"], out, rc)
    rc, out = c.build(r, "build", "--all"); c.check(r, "S6c build --all", [f"p{i}" for i in range(30)] + ["index"], out, rc)
    nb = [c.built_count(r, "S6d no-op") for _ in range(5)]
    if any(nb): c.fails.append(f"S6d no-op after --all rebuilt {nb}")
    # S8 mode switching modified -> hash -> modified, with edits
    set_mode(r, "hash"); rc, out = c.build(r, "build"); c.check(r, "S8a switch to hash", None, out, rc)
    c.notes.append(f"S8a switch modified->hash rebuilt {len(built(out))}")
    c.edit(r, "content/p3.html", "<p>p3 hash edit</p>\n")
    rc, out = c.build(r, "build"); c.check(r, "S8b edit in hash mode", ["p3"], out, rc)
    set_mode(r, "modified"); ticks.age(r); c.floor = ticks.sequence([f"{r}/.nift/config.json"]); rc, out = c.build(r, "build"); c.check(r, "S8c switch back to modified", None, out, rc)
    c.notes.append(f"S8c switch hash->modified rebuilt {len(built(out))}")
    c.edit(r, "templates/part.html", "PART3\n")
    rc, out = c.build(r, "build"); c.check(r, "S8d edit after switching back", [f"p{i}" for i in range(10)], out, rc)
    nb = [c.built_count(r, "S6d no-op") for _ in range(5)]
    if any(nb): c.fails.append(f"S8e no-op after mode switches rebuilt {nb}")

def scen_interrupt(c, delays):
    for d in delays:
        r = tempfile.mkdtemp(dir=BASE); mksite(r, 1500, pad=3000)
        rc0, out0 = c.build(r, "build", "--all")
        if rc0 != 0: c.fails.append(f"K setup build failed rc={rc0}")
        c.edit(r, "templates/template.html", '<html><head>@input("templates/head.html")</head><body>TPL-K @content</body></html>\n')
        p = subprocess.Popen([c.nift, "build"], cwd=r, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(d); killed = p.poll() is None
        if killed: os.kill(p.pid, signal.SIGKILL)
        p.wait()
        state = "killed" if killed else "finished-before-kill"
        if not killed:
            c.fails.append(f"K kill@{d}s: interruption did not happen (build finished before SIGKILL)")
        stale_now = sum(1 for f in os.listdir(f"{r}/public") if "TPL-K" not in open(f"{r}/public/{f}").read() and f.startswith("p") and int(f[1:-5]) >= 10)
        rc, out = c.build(r, "build"); nb1 = len(built(out)); first = out.strip().splitlines()[:2]
        ok1 = rc == 0 and tree(f"{r}/public") == clean_oracle(c.nift, r, c.fails)
        if rc != 0:
            c.fails.append(f"K kill@{d}s: plain recovery failed rc={rc}: {first}")
        if not ok1 and rc == 0:
            c.fails.append(f"K kill@{d}s: recovered output differs from clean oracle")
        nb = [c.built_count(r, "K settle") for _ in range(3)]
        if any(nb): c.fails.append(f"K kill@{d}s: recovery did not settle: {nb}")
        c.notes.append(f"K kill@{d}s {state}: recovery build rc={rc} rebuilt {nb1}, then {nb}; oracle_ok={ok1}")
        shutil.rmtree(r)

def scen_crashpoints(c):
    # NIFT_TEST_CRASH_AT points exist in Runtime 2.0 binaries only.
    for point in ("page-rendered:p12", "page-output:p12", "page-info:p12", "page-post:p12", "page-done:p12"):
        r = tempfile.mkdtemp(dir=BASE); mksite(r, 30)
        rc0, out0 = c.build(r, "build", "--all")
        if rc0 != 0: c.fails.append(f"C setup build failed rc={rc0}")
        c.edit(r, "templates/template.html", '<html><head>@input("templates/head.html")</head><body>TPL-C @content</body></html>\n')
        rc, out = c.build(r, "build", env={"NIFT_TEST_CRASH_AT": point})
        if rc not in (-9, 137):
            c.fails.append(f"C {point}: injected crash did not terminate by SIGKILL (rc={rc})")
        rc2, out2 = c.build(r, "build")
        ok = rc2 == 0 and tree(f"{r}/public") == clean_oracle(c.nift, r, c.fails)
        if rc2 != 0:
            c.fails.append(f"C {point}: plain recovery failed rc={rc2}")
        elif not ok:
            c.fails.append(f"C {point}: recovered output differs from clean oracle")
        nb = [c.built_count(r, "K settle") for _ in range(3)]
        if any(nb): c.fails.append(f"C {point}: recovery did not settle: {nb}")
        c.notes.append(f"C {point}: crash rc={rc}, recovery rebuilt {len(built(out2))}, then {nb}, oracle_ok={ok}")
        shutil.rmtree(r)

def scen_immediate(c, cycles):
    # IMMEDIATE (outside "ordinary": no sleep). Edit right AFTER a build returns.
    r = tempfile.mkdtemp(dir=BASE); mksite(r, 3)
    rc0, out0 = run(c.nift, r, "build", "--all")
    if rc0 != 0: c.fails.append(f"I setup build failed rc={rc0}")
    miss = persistent = 0
    for i in range(cycles):
        write(f"{r}/content/p12.html" if False else f"{r}/content/p1.html", f"MARK-{i}\n")
        rc, _ = run(c.nift, r, "build")
        if rc != 0: c.fails.append(f"I immediate build failed rc={rc} at cycle {i}")
        if f"MARK-{i}" not in open(f"{r}/public/p1.html").read():
            miss += 1
            for _ in range(3):
                rc, _ = run(c.nift, r, "build")
                if rc != 0: c.fails.append(f"I follow-up build failed rc={rc} at cycle {i}")
            if f"MARK-{i}" not in open(f"{r}/public/p1.html").read():
                persistent += 1
                ticks.age(r); write(f"{r}/content/p1.html", f"REPAIR-{i}\n"); ticks.sequence([f"{r}/content/p1.html"])
                rc, _ = run(c.nift, r, "build")
                if rc != 0: c.fails.append(f"I repair build failed rc={rc} at cycle {i}")
    c.notes.append(f"I immediate edit->build {cycles} cycles: missed {miss}, persistent after 3 more builds {persistent}")
    if miss: c.fails.append(f"I immediate edits: {miss}/{cycles} missed on the first post-edit build")
    if persistent: c.fails.append(f"I immediate edits: {persistent}/{cycles} PERSISTENTLY stale")

if __name__ == "__main__":
    which = os.environ.get("ONLY", "basic,interrupt,crash,immediate").split(",")
    failed = 0
    for spec in sys.argv[1:]:
        label, nift = spec.split("=", 1)
        c = Ctx(label, nift)
        if "basic" in which: scen_basic(c)
        if "interrupt" in which: scen_interrupt(c, [0.01, 0.03, 0.06])
        if "crash" in which and label != "v412": scen_crashpoints(c)
        if "immediate" in which: scen_immediate(c, int(os.environ.get("CYCLES", "300")))
        print(f"=== {label} ({nift})\nFAILS ({len(c.fails)}):")
        for f in c.fails: print("  FAIL", f)
        for n in c.notes: print("  note", n)
        sys.stdout.flush()
        if c.fails: failed = 1
    shutil.rmtree(BASE, ignore_errors=True)
    sys.exit(failed)
