#!/usr/bin/env python3
"""feature-build-recovery (D-020): loud errors, automatic safe recovery.

Ordinary build failures (expression/template errors, missing dependencies,
failing scripts and hooks, pagination failures) end non-zero, keep last-good
outputs, leave no .nift/.unfinished, and the next build works once the
problem is fixed - never needing --repair. A build SIGKILLed at every named
point of the epoch (NIFT_TEST_CRASH_AT) leaves the marker, and the next plain
build recovers automatically to a state byte-identical (outputs + page-info) to
a from-scratch build. Only genuinely uncertain state (a Nift-owned write
failing) keeps the marker, with a clear message, and is recovered by the next
build once the cause is gone. Live builds are still refused.
"""
import json, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

NIFT = str(Path(os.environ.get("NIFT_BIN", "./nift")).resolve())
MODES = os.environ.get("RECOVERY_MODES", "modified,hash,hybrid").split(",")
fails = []
checks = 0


def check(label, cond, detail=""):
    global checks
    checks += 1
    if not cond:
        fails.append(label)
        print(f"  FAIL {label}" + (f": {detail}" if detail else ""))


def run(root, *args, env=None):
    e = dict(os.environ)
    e.pop("NIFT_TEST_CRASH_AT", None)
    if env:
        e.update(env)
    p = subprocess.run([NIFT, *args], cwd=root, env=e, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True, timeout=60)
    return p.returncode, p.stdout + p.stderr


def marker(root):
    return (root / ".nift/.unfinished").exists()


SOURCES = ("content", "templates", "data", "scripts")


def write(root, rel, text):
    p = root / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


def project(root, mode):
    """Plain pages, a paginated page, a page with a post-build item script,
    a page with a legacy per-file post hook, a custom-build page and a
    prerequisite chain."""
    (root / ".nift").mkdir(parents=True)
    (root / "public").mkdir()
    cfg = {"config": {"content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
                      "output-ext": ".html", "default-template": "templates/t.html",
                      "build-threads": -1, "incremental-mode": mode}}
    (root / ".nift/config.json").write_text(json.dumps(cfg))
    tracked = [
        {"name": "a", "title": "A", "template": "templates/t.html"},
        {"name": "b", "title": "B", "template": "templates/t.html"},
        {"name": "c", "title": "C", "template": "templates/t.html"},
        {"name": "list", "title": "List", "template": "templates/t.html", "paginate": {"items-per-page": 2}},
        {"name": "post", "title": "Post", "template": "templates/t.html", "post-build": "scripts/post.f"},
        {"name": "legacy", "title": "Legacy", "template": "templates/t.html", "post build": "scripts/legacy.f"},
        {"name": "custom", "title": "Custom", "template": "templates/t.html", "build": "scripts/custom.f"},
        {"name": "base", "title": "Base", "template": "templates/t.html"},
        {"name": "child", "title": "Child", "template": "templates/t.html", "depends": ["base"]},
    ]
    (root / ".nift/tracked.json").write_text(json.dumps({"tracked": tracked}))
    write(root, "templates/t.html", "<title>$[title]</title><main>@content</main>\n")
    write(root, "data/shared.txt", "shared-1\n")
    for n in ("a", "b", "c", "base", "child", "post", "legacy", "custom"):
        write(root, f"content/{n}.html", f"<p>{n}</p>@input(\"data/shared.txt\")\n")
    write(root, "data/items.json", json.dumps({"items": ["one", "two", "three", "four", "five"]}))
    write(root, "content/list.html",
          "@json(d, 'data/items.json')\n@paginate\n@for(x : d.items){@item{<b>$[x]</b>}}\n")
    write(root, "content/list.paginate.html", "<section>$[paginate.items]</section>$[paginate.current]/$[paginate.total]\n")
    write(root, "scripts/post.f", 'print("post ok")\n')
    write(root, "scripts/legacy.f", 'print("legacy ok")\n')
    write(root, "scripts/custom.f",
          'out := file(getenv("NIFT_HOOK_OUTPUT"))\nout.open("w")\nout.write("custom-1")\nout.save()\nout.close()\n')


def snapshot(root):
    """Outputs and page-info (reproducible state). The CP01 clock records are
    timing state by design and are checked for presence only."""
    snap = {}
    for base in ("public", ".nift/public"):
        d = root / base
        if d.exists():
            for p in sorted(d.rglob("*")):
                if p.is_file():
                    snap[p.relative_to(root).as_posix()] = p.read_bytes()
    return snap


def oracle(root, scratch):
    """From-scratch build of the current sources."""
    fresh = scratch / ("oracle-" + str(time.monotonic_ns()))
    fresh.mkdir()
    (fresh / ".nift").mkdir()
    (fresh / "public").mkdir()
    for f in ("config.json", "tracked.json"):
        shutil.copy(root / ".nift" / f, fresh / ".nift" / f)
    for d in SOURCES:
        if (root / d).exists():
            shutil.copytree(root / d, fresh / d)
    rc, out = run(fresh, "build", "--all")
    assert rc == 0, out
    snap = snapshot(fresh)
    shutil.rmtree(fresh)
    return snap


def same_as_clean(root, scratch, label):
    got, want = snapshot(root), oracle(root, scratch)
    diff = sorted(k for k in set(got) | set(want) if got.get(k) != want.get(k))
    check(f"{label}: output + page-info byte-identical to a clean build", not diff, ", ".join(diff[:6]))
    check(f"{label}: no failed-page list or backup files left",
          not (root / ".nift/failed.json").exists() and not list(root.rglob("*.nift-tmp-*")))


def _mtimes(root):
    return {p.relative_to(root).as_posix(): p.stat().st_mtime_ns for p in root.rglob("*") if p.is_file()}


def _equal_tick(before, out):
    """True only if every page the build rebuilt was rebuilt solely because a
    dependency's mtime EQUALS that page's page-info mtime, both read from disk
    before the build (same rule as tests/build_recovery_random.py)."""
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


equal_tick_allowances = []


def settle(root):
    """The build after a successful build must be a no-op. modified/hybrid
    rebuild conservatively when a dependency's mtime EQUALS the page-info
    mtime (CP01 rule): on a multigrain-timestamp kernel an idle host stamps a
    page-info written in the same tick as the source edit with the source's
    exact nanosecond mtime. So exactly one extra rebuild is allowed, only when
    the tester confirms that condition from disk, and only if that rebuild
    changes no output or page-info byte (the output was not stale: the extra
    build was wasted work, not a correction). The build after it must be a
    no-op. Returns (ok, allowed, output)."""
    before, snap = _mtimes(root), snapshot(root)
    rc, out = run(root, "build")
    if rc == 0 and "up to date" in out:
        return True, False, out
    if not (rc == 0 and _equal_tick(before, out)):
        return False, False, out
    after = snapshot(root)
    changed = sorted(k for k in set(snap) | set(after) if snap.get(k) != after.get(k))
    if changed:
        return False, True, f"equal-timestamp rebuild changed {', '.join(changed[:6])} (output was stale)\n{out}"
    rc, out2 = run(root, "build")
    return rc == 0 and "up to date" in out2, True, out + out2


def up_to_date(root, label):
    ok, allowed, out = settle(root)
    if ok and allowed:
        equal_tick_allowances.append(label)
    check(f"{label}: following build is a no-op", ok, out[-600:])


def equal_tick_allowance_contract(scratch):
    """The allowance is exercised deterministically (forced equal stamps) and
    cannot hide a stale output: equal stamps + unchanged bytes is allowed once;
    equal stamps + changed bytes must rebuild with the new content and is
    never accepted as a no-op."""
    for mode in ("modified", "hybrid"):
        root = scratch / f"equal-tick-{mode}"
        project(root, mode)
        run(root, "build")
        info_ns = (root / ".nift/public/a.info.json").stat().st_mtime_ns
        os.utime(root / "content/a.html", ns=(info_ns, info_ns))
        ok, allowed, out = settle(root)
        if mode == "hybrid":
            check(f"equal-tick {mode}: unchanged source with equal stamp is a confirmed allowance", ok and allowed, out[-400:])
        else:  # modified mode: no tick-hash record for a forced stamp, same CP01 rule
            check(f"equal-tick {mode}: unchanged source with equal stamp settles", ok, out[-400:])
        stale = (root / "public/a.html").read_bytes()
        write(root, "content/a.html", "<p>a-equal-tick</p>\n")
        info_ns = (root / ".nift/public/a.info.json").stat().st_mtime_ns
        os.utime(root / "content/a.html", ns=(info_ns, info_ns))
        ok, allowed, out = settle(root)
        check(f"equal-tick {mode}: changed source with equal stamp is never a no-op", not ok, out[-400:])
        check(f"equal-tick {mode}: changed source with equal stamp is rebuilt",
              (root / "public/a.html").read_bytes() != stale and b"a-equal-tick" in (root / "public/a.html").read_bytes())
        same_as_clean(root, scratch, f"equal-tick {mode}")
        shutil.rmtree(root)


# ---------------------------------------------------------------- ordinary failures

def ordinary_failures(mode, scratch):
    root = scratch / f"ord-{mode}"
    project(root, mode)
    rc, out = run(root, "build")
    check(f"{mode}: baseline build", rc == 0, out)

    # (1) partial page failure: a and b change, b fails (missing dependency),
    # c has an expression error; a commits, b/c keep their last-good output.
    good_b = (root / "public/b.html").read_bytes()
    good_c = (root / "public/c.html").read_bytes()
    info_b = (root / ".nift/public/b.info.json").read_bytes()
    write(root, "content/a.html", "<p>a2</p>\n")
    write(root, "content/b.html", '<p>b2</p>@input("data/missing.txt")\n')
    write(root, "content/c.html", "<p>c2</p>$[1 +]\n")
    rc, out = run(root, "build")
    check(f"{mode}: partial failure exits non-zero", rc != 0, out)
    check(f"{mode}: partial failure is loud", "error" in out and "while building b" in out, out)
    check(f"{mode}: no .unfinished after an ordinary failure", not marker(root))
    check(f"{mode}: succeeded page committed", (root / "public/a.html").read_text().count("a2") == 1)
    check(f"{mode}: failed page keeps last-good output (b)", (root / "public/b.html").read_bytes() == good_b)
    check(f"{mode}: failed page keeps its last-good page-info", (root / ".nift/public/b.info.json").read_bytes() == info_b)
    check(f"{mode}: failed pages listed in .nift/failed.json",
          set(json.loads((root / ".nift/failed.json").read_text())["failed"]) == {"b", "c"})
    check(f"{mode}: expression error reported", "while building c" in out, out)
    check(f"{mode}: failed page keeps last-good output (c)", (root / "public/c.html").read_bytes() == good_c)
    rc, out = run(root, "status")
    check(f"{mode}: status names the failed page and reason",
          "previous build of this page failed" in out and "\nb\n" in out and "\na\n" not in out, out)
    # Repeating without a fix fails the same way and still leaves no marker.
    rc, out = run(root, "build")
    check(f"{mode}: unfixed rebuild fails again, no marker", rc != 0 and not marker(root), out)
    # Fix -> ordinary build succeeds, no --repair.
    write(root, "content/b.html", "<p>b3</p>\n")
    write(root, "content/c.html", "<p>c3</p>\n")
    rc, out = run(root, "build")
    check(f"{mode}: build after the fix succeeds without --repair", rc == 0 and "repair" not in out, out)
    check(f"{mode}: fixed page rebuilt", "b3" in (root / "public/b.html").read_text())
    up_to_date(root, f"{mode} partial")
    same_as_clean(root, scratch, f"{mode} partial")

    # (2) failure under --all and a targeted build.
    good_b = (root / "public/b.html").read_bytes()
    write(root, "content/b.html", "@input(\"data/missing.txt\")\n")
    rc, out = run(root, "build", "--all")
    check(f"{mode}: --all with a failing page exits non-zero, no marker", rc != 0 and not marker(root), out)
    check(f"{mode}: --all keeps the failed page's last-good output", (root / "public/b.html").read_bytes() == good_b)
    rc, out = run(root, "build", "b")
    check(f"{mode}: targeted failing build exits non-zero, no marker", rc != 0 and not marker(root), out)
    write(root, "content/b.html", "<p>b4</p>\n")
    rc, out = run(root, "build", "b")
    check(f"{mode}: targeted build after the fix succeeds", rc == 0, out)
    up_to_date(root, f"{mode} targeted")
    same_as_clean(root, scratch, f"{mode} --all/targeted")

    # (3) prerequisite failure blocks its dependent; both recover after the fix.
    good_child = (root / "public/child.html").read_bytes()
    write(root, "content/base.html", "@input(\"data/missing.txt\")\n")
    write(root, "content/child.html", "<p>child2</p>\n")
    rc, out = run(root, "build")
    check(f"{mode}: failed prerequisite blocks dependent, no marker",
          rc != 0 and "blocked because prerequisite 'base' failed" in out and not marker(root), out)
    check(f"{mode}: blocked dependent keeps last-good output", (root / "public/child.html").read_bytes() == good_child)
    write(root, "content/base.html", "<p>base2</p>\n")
    rc, out = run(root, "build")
    check(f"{mode}: prerequisite fix rebuilds both", rc == 0 and "child2" in (root / "public/child.html").read_text(), out)
    up_to_date(root, f"{mode} depends")
    same_as_clean(root, scratch, f"{mode} depends")

    # (4) per-page scripts failing AFTER the output was written: last-good
    # output restored (post-build item script, legacy post hook), custom build
    # failing after writing a partial output.
    for page, script, broken in (("post", "scripts/post.f", 'nosuchfunction_xyz()\n'),
                                 ("legacy", "scripts/legacy.f", 'nosuchfunction_xyz()\n'),
                                 ("custom", "scripts/custom.f",
                                  'out := file(getenv("NIFT_HOOK_OUTPUT"))\nout.open("w")\nout.write("PARTIAL")\n'
                                  'out.save()\nout.close()\nnosuchfunction_xyz()\n')):
        good = (root / f"public/{page}.html").read_bytes()
        original = (root / script).read_text()
        write(root, f"content/{page}.html", f"<p>{page}-new</p>\n")
        write(root, script, broken)
        rc, out = run(root, "build")
        check(f"{mode}: failing {page} script exits non-zero, no marker", rc != 0 and not marker(root), out)
        check(f"{mode}: failing {page} script restores last-good output",
              (root / f"public/{page}.html").read_bytes() == good,
              (root / f"public/{page}.html").read_text()[:80])
        check(f"{mode}: no backup files left by {page}",
              not list((root / "public").glob("*.nift-tmp-*")))
        rc, out = run(root, "status")
        check(f"{mode}: {page} stays dirty", "previous build of this page failed" in out, out)
        write(root, script, original)
        rc, out = run(root, "build")
        check(f"{mode}: {page} rebuilt after the script fix", rc == 0, out)
    up_to_date(root, f"{mode} scripts")
    same_as_clean(root, scratch, f"{mode} scripts")

    # (5) pagination failure: 3 pages; failure keeps all three; fix with fewer
    # items removes the stale page.
    good = {n: (root / f"public/{n}").read_bytes() for n in ("list.html", "list-2.html", "list-3.html")}
    write(root, "content/list.paginate.html", "<section>$[paginate.items]</section>$[1 +]\n")
    write(root, "data/items.json", json.dumps({"items": ["one", "two", "three", "four", "five", "six", "seven"]}))
    rc, out = run(root, "build")
    check(f"{mode}: pagination failure exits non-zero, no marker", rc != 0 and not marker(root), out)
    check(f"{mode}: pagination failure keeps every last-good page",
          all((root / f"public/{n}").read_bytes() == b for n, b in good.items())
          and not (root / "public/list-4.html").exists())
    write(root, "content/list.paginate.html", "<section>$[paginate.items]</section>$[paginate.current]/$[paginate.total]\n")
    write(root, "data/items.json", json.dumps({"items": ["one", "two", "three"]}))
    rc, out = run(root, "build")
    check(f"{mode}: pagination fix succeeds", rc == 0, out)
    check(f"{mode}: stale pagination page removed after the fix", not (root / "public/list-3.html").exists())
    up_to_date(root, f"{mode} pagination")
    same_as_clean(root, scratch, f"{mode} pagination")

    # (6) project hooks: pre failure aborts before pages; post failure after
    # every page committed. Neither leaves a marker.
    cfg_path = root / ".nift/config.json"
    cfg = json.loads(cfg_path.read_text())
    write(root, "scripts/bad.f", "nosuchfunction_xyz()\n")
    for phase in ("pre build", "post build"):
        before = snapshot(root)
        cfg["config"][phase] = "scripts/bad.f"
        cfg_path.write_text(json.dumps(cfg))
        write(root, "content/a.html", f"<p>a-{phase}</p>\n")
        rc, out = run(root, "build")
        check(f"{mode}: failing project '{phase}' hook exits non-zero and is loud",
              rc != 0 and "build hook" in out and "failed" in out, out)
        check(f"{mode}: failing project '{phase}' hook leaves no marker", not marker(root))
        if phase == "pre build":
            check(f"{mode}: pre hook failure changes no output", snapshot(root) == before)
        else:
            check(f"{mode}: post hook failure keeps committed pages", f"a-{phase}" in (root / "public/a.html").read_text())
        del cfg["config"][phase]
        cfg_path.write_text(json.dumps(cfg))
        rc, out = run(root, "build")
        check(f"{mode}: build after the hook fix succeeds", rc == 0, out)
    up_to_date(root, f"{mode} hooks")
    same_as_clean(root, scratch, f"{mode} hooks")
    shutil.rmtree(root)


# ---------------------------------------------------------------- crashes

PAGE_POINTS = ["page-start", "page-rendered", "page-output-partial", "page-output", "page-post", "page-info",
               "page-done"]
# page-failed and failed-list are exercised with a failing page below.
EPOCH_POINTS = ["acquired", "reconciled", "pre-hooks", "pages-done", "post-hooks"]


def edit_round(root, i):
    write(root, "data/shared.txt", f"shared-{i}\n")
    write(root, "data/items.json", json.dumps({"items": [f"x{j}" for j in range(1 + (i % 6))]}))
    write(root, "content/post.html", f"<p>post {i}</p>\n")
    write(root, "content/custom.html", f"<p>custom {i}</p>\n")


def crash_case(mode, scratch, point, page, i, extra_args=()):
    root = scratch / f"crash-{mode}"
    edit_round(root, i)
    spec = point + (f":{page}" if page else "")
    rc, out = run(root, "build", *extra_args, env={"NIFT_TEST_CRASH_AT": spec})
    label = f"{mode} kill@{spec}{' ' + ' '.join(extra_args) if extra_args else ''}"
    if rc in (-9, 137):
        check(f"{label}: marker survives the kill", marker(root))
        rc, out = run(root, "build")
        check(f"{label}: next plain build recovers automatically", rc == 0 and "Recovering automatically" in out, out[-600:])
        check(f"{label}: marker cleared by the recovery", not marker(root))
    else:
        check(f"{label}: crash point reached", False, f"rc={rc} {out[-300:]}")
        return
    up_to_date(root, label)
    same_as_clean(root, scratch, label)


def crashes(mode, scratch):
    root = scratch / f"crash-{mode}"
    project(root, mode)
    rc, out = run(root, "build")
    check(f"{mode}: crash baseline", rc == 0, out)
    i = 1
    for point in EPOCH_POINTS:
        crash_case(mode, scratch, point, None, i); i += 1
    for point in PAGE_POINTS:
        for page in ("a", "list", "post", "custom"):
            if point == "page-output-partial" and page == "custom":
                continue  # a custom build writes its own output
            crash_case(mode, scratch, point, page, i); i += 1
    # Targeted and --all builds killed.
    crash_case(mode, scratch, "page-output-partial", "a", i, ("a",)); i += 1
    crash_case(mode, scratch, "page-info", "list", i, ("--all",)); i += 1

    # Kill while settling a failed page, then recover while the page still fails.
    write(root, "content/b.html", '@input("data/missing.txt")\n')
    rc, out = run(root, "build", env={"NIFT_TEST_CRASH_AT": "page-failed:b"})
    check(f"{mode} kill@page-failed: killed with marker", rc in (-9, 137) and marker(root), out)
    rc, out = run(root, "build", env={"NIFT_TEST_CRASH_AT": "failed-list"})
    check(f"{mode} kill@failed-list (inside the recovery): killed with marker", rc in (-9, 137) and marker(root), out[-300:])
    rc, out = run(root, "build")
    check(f"{mode} recovery with a still-failing page: loud, non-zero, marker cleared",
          rc != 0 and "Recovering automatically" in out and "while building b" in out and not marker(root), out[-600:])
    write(root, "content/b.html", "<p>b fixed</p>\n")
    rc, out = run(root, "build")
    check(f"{mode}: build after recovery + fix succeeds", rc == 0, out)
    up_to_date(root, f"{mode} recovery-with-failure")
    same_as_clean(root, scratch, f"{mode} recovery-with-failure")

    # Kill during the automatic recovery itself; the next build recovers again.
    write(root, "content/a.html", "<p>a crash-in-recovery</p>\n")
    run(root, "build", env={"NIFT_TEST_CRASH_AT": "page-output-partial:a"})
    for spec in ("page-output-partial:list", "repair-sweep"):
        rc, out = run(root, "build", env={"NIFT_TEST_CRASH_AT": spec})
        check(f"{mode}: kill@{spec} during recovery keeps the marker", rc in (-9, 137) and marker(root), out[-300:])
    rc, out = run(root, "build")
    check(f"{mode}: recovery after a killed recovery", rc == 0 and not marker(root), out[-400:])
    up_to_date(root, f"{mode} killed-recovery")
    same_as_clean(root, scratch, f"{mode} killed-recovery")

    # Stale marker + targeted build recovers the whole project; explicit
    # --repair still works.
    edit_round(root, 100)
    rc, out = run(root, "build", env={"NIFT_TEST_CRASH_AT": "page-output"})
    check(f"{mode}: killed before a targeted build", rc in (-9, 137) and marker(root), out[-300:])
    rc, out = run(root, "build", "c")
    check(f"{mode}: targeted build on a stale marker recovers", rc == 0 and "Recovering automatically" in out and not marker(root), out)
    run(root, "build", env={"NIFT_TEST_CRASH_AT": "acquired"})
    rc, out = run(root, "untrack", "c")
    check(f"{mode}: untrack on a stale marker refuses and points at nift build",
          rc != 0 and "nift build" in out and marker(root), out)
    rc, out = run(root, "build", "--repair")
    check(f"{mode}: explicit --repair still recovers", rc == 0 and not marker(root), out)
    same_as_clean(root, scratch, f"{mode} repair")
    shutil.rmtree(root)


# ---------------------------------------------------------------- uncertain + concurrency

def uncertain_and_concurrency(scratch):
    root = scratch / "uncertain"
    project(root, "modified")
    run(root, "build")
    # A Nift-owned write failing is not an ordinary error: the marker is kept
    # with a clear message, and the next build recovers once the cause is gone.
    write(root, "content/a.html", "<p>a io</p>\n")
    (root / "public/a.html").unlink()
    (root / "public/a.html").mkdir()
    rc, out = run(root, "build")
    check("I/O failure keeps the marker with a clear message",
          rc != 0 and marker(root) and "recovers automatically" in out, out)
    (root / "public/a.html").rmdir()
    rc, out = run(root, "build")
    check("next build recovers once the I/O cause is gone", rc == 0 and not marker(root) and "Recovering automatically" in out, out)
    same_as_clean(root, scratch, "uncertain")

    # A damaged failed-page list is distrusted: every page is rebuilt.
    (root / ".nift/failed.json").write_text("{ torn")
    rc, out = run(root, "status")
    check("damaged failed list marks every page dirty", out.count("previous build of this page failed") == 9, out[-300:])
    rc, out = run(root, "build")
    check("damaged failed list: build rebuilds and removes it", rc == 0 and not (root / ".nift/failed.json").exists(), out[-300:])
    same_as_clean(root, scratch, "damaged failed list")

    # Two builds at once: the second is refused while the first owns the
    # lock, then the first completes and clears the marker.
    hold = scratch / "hold"
    hold.mkdir()
    write(root, "content/a.html", "<p>a concurrent</p>\n")
    e = dict(os.environ, NIFT_TEST_OWNERSHIP_HOLD=str(hold))
    first = subprocess.Popen([NIFT, "build"], cwd=root, env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    deadline = time.time() + 30
    while not (hold / "acquired").exists() and time.time() < deadline:
        time.sleep(0.01)
    rc, out = run(root, "build")
    check("concurrent build refused while another owns the lock", rc == 1 and "another build appears to be running" in out, out)
    rc, out = run(root, "build", "--repair")
    check("concurrent --repair refused while another owns the lock", rc == 1 and "another build appears to be running" in out, out)
    check("live owner's marker untouched by refused builds", marker(root))
    (hold / "release").touch()
    out, _ = first.communicate(timeout=60)
    check("first build completes after release", first.returncode == 0 and not marker(root), out)
    # Concurrent builds without coordination: every invocation is either a
    # success or a clean refusal, and the end state is consistent.
    procs = [subprocess.Popen([NIFT, "build", "--all"], cwd=root, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True) for _ in range(4)]
    results = [(p.wait(timeout=120), p.stdout.read()) for p in procs]
    check("racing builds: each succeeds or is refused as live",
          all(rc == 0 or (rc == 1 and "another build appears to be running" in o) for rc, o in results),
          str([(rc, o[-120:]) for rc, o in results]))
    check("racing builds: no marker left", not marker(root))
    up_to_date(root, "racing")
    same_as_clean(root, scratch, "racing")
    shutil.rmtree(root)



# ---------------------------------------------------------------- review fixes

def corrupt_failed_lists(scratch):
    """F1: any malformed failed list (huge/absurd numbers, wrong shapes) is
    treated as damaged: bounded, conservative full rebuild, clean result."""
    root = scratch / "corrupt-list"
    project(root, "modified")
    run(root, "build")
    for text in ('{"failed":{"a":100000000000}}', '{"failed":{"post":1e300}}', '{"failed":["a",1e300]}',
                 '{"failed":[["a"]]}', '{"failed":"a"}', '[]', '{"failed":["a"],"x":', '\u0000'):
        (root / ".nift/failed.json").write_text(text)
        for args in (("build",), ("build", "--repair"), ("status",)):
            try:
                rc, out = run(root, *args) if args[0] != "status" else run(root, "status")
            except subprocess.TimeoutExpired:
                check(f"corrupt list {text[:24]!r}: {' '.join(args)} finishes (no hang)", False)
                continue
            if args[0] == "build":
                check(f"corrupt list {text[:24]!r}: {' '.join(args)} succeeds", rc == 0, out[-300:])
                (root / ".nift/failed.json").write_text(text)
        rc, out = run(root, "build")
        check(f"corrupt list {text[:24]!r}: converges", rc == 0 and not (root / ".nift/failed.json").exists(), out[-300:])
        same_as_clean(root, scratch, f"corrupt list {text[:24]!r}")
    shutil.rmtree(root)


def paginated_script_project(root, mode="modified"):
    project(root, mode)
    t = json.loads((root / ".nift/tracked.json").read_text())
    for e in t["tracked"]:
        if e["name"] == "list":
            e["post-build"] = "scripts/lpost.f"
    (root / ".nift/tracked.json").write_text(json.dumps(t))
    write(root, "scripts/lpost.f", 'print("ok")\n')
    rc, out = run(root, "build")
    assert rc == 0, out


def list_files(root):
    return sorted(p.name for p in (root / "public").glob("list*"))


def pagination_without_failed_list(scratch):
    """F2: extra pagination pages a failed attempt wrote are removed at
    failure time, so neither a deleted nor a damaged failed list, nor a
    later --repair, can leave them behind."""
    for variant in ("deleted", "damaged", "repair", "kept"):
        root = scratch / f"pag-{variant}"
        paginated_script_project(root)
        before = list_files(root)
        write(root, "data/items.json", json.dumps({"items": [str(i) for i in range(9)]}))
        write(root, "scripts/lpost.f", "nosuchfunction_xyz()\n")
        rc, out = run(root, "build")
        check(f"pagination/{variant}: failing attempt exits non-zero, no marker", rc != 0 and not marker(root), out[-300:])
        check(f"pagination/{variant}: extra pages of the failed attempt removed", list_files(root) == before,
              f"{list_files(root)} vs {before}")
        if variant == "deleted":
            (root / ".nift/failed.json").unlink()
        elif variant == "damaged":
            (root / ".nift/failed.json").write_text("{torn")
        write(root, "scripts/lpost.f", 'print("ok")\n')
        write(root, "data/items.json", json.dumps({"items": ["a"]}))
        rc, out = run(root, *(("build", "--repair") if variant == "repair" else ("build",)))
        check(f"pagination/{variant}: fixed build succeeds", rc == 0, out[-300:])
        if variant == "deleted":
            run(root, "build", "--all")  # the page itself is no longer known dirty
        up_to_date(root, f"pagination/{variant}")
        same_as_clean(root, scratch, f"pagination/{variant}")
        shutil.rmtree(root)
    # Interrupted after the extra pages were written: the recovery sweep
    # removes them (namespace of a currently paginated page).
    root = scratch / "pag-killed"
    paginated_script_project(root)
    write(root, "data/items.json", json.dumps({"items": [str(i) for i in range(9)]}))
    rc, out = run(root, "build", env={"NIFT_TEST_CRASH_AT": "page-output:list"})
    check("pagination/killed: killed after writing 5 pages", rc in (-9, 137) and (root / "public/list-5.html").exists())
    (root / ".nift/failed.json").unlink(missing_ok=True)
    write(root, "data/items.json", json.dumps({"items": ["a"]}))
    rc, out = run(root, "build")
    check("pagination/killed: recovery succeeds", rc == 0 and "Recovering automatically" in out, out[-300:])
    same_as_clean(root, scratch, "pagination/killed")
    shutil.rmtree(root)


def symlinked_outputs(scratch):
    """F3: a symlinked output stays a symlink through success, failure
    (last-good written back through the link) and repair; inside-project,
    outside-project and broken links."""
    outside = scratch / "outside"
    outside.mkdir(exist_ok=True)
    for kind in ("outside", "inside", "broken"):
        root = scratch / f"link-{kind}"
        project(root, "modified")
        run(root, "build")
        link = root / "public/post.html"
        if kind == "outside":
            target = outside / "post-real.html"
        elif kind == "inside":
            (root / "store").mkdir()
            target = root / "store/post-real.html"
        else:
            target = outside / "missing-dir-free" / "post-dangling.html"
            target.parent.mkdir(exist_ok=True)
        if kind != "broken":
            shutil.copy(link, target)
            os.chmod(target, 0o644)
        link.unlink()
        os.symlink(target, link)
        write(root, "content/post.html", "<p>post-v2</p>\n")
        rc, out = run(root, "build")
        check(f"symlink/{kind}: successful build keeps the link", rc == 0 and link.is_symlink(), out[-200:])
        check(f"symlink/{kind}: target written through the link", target.exists() and b"post-v2" in target.read_bytes())
        good = target.read_bytes()
        write(root, "content/post.html", "<p>post-v3</p>\n")
        write(root, "scripts/post.f", "nosuchfunction_xyz()\n")
        rc, out = run(root, "build")
        check(f"symlink/{kind}: failure keeps the link", rc != 0 and link.is_symlink() and not marker(root), out[-200:])
        check(f"symlink/{kind}: target holds the last-good bytes", target.read_bytes() == good)
        check(f"symlink/{kind}: no backup files left", not list(root.rglob("*.nift-tmp-*")))
        write(root, "scripts/post.f", 'print("post ok")\n')
        run(root, "build", env={"NIFT_TEST_CRASH_AT": "page-output-partial:post"})
        rc, out = run(root, "build")
        check(f"symlink/{kind}: recovery after a kill keeps the link", rc == 0 and link.is_symlink(), out[-200:])
        rc, out = run(root, "build", "--repair")
        check(f"symlink/{kind}: --repair keeps the link", rc == 0 and link.is_symlink() and b"post-v3" in target.read_bytes(), out[-200:])
        shutil.rmtree(root)


def failed_list_hygiene(scratch):
    """F6: untracked/removed/moved names leave the list (absent when empty);
    a re-tracked name starts clean; a no-op build prunes stale names."""
    root = scratch / "hygiene"
    project(root, "modified")
    run(root, "build")
    write(root, "content/b.html", '@input("data/missing.txt")\n')
    run(root, "build")
    rc, out = run(root, "untrack", "b")
    check("hygiene: untrack drops the name, list absent when empty",
          rc == 0 and not (root / ".nift/failed.json").exists(), out)
    write(root, "content/b.html", "<p>b back</p>\n")
    rc, out = run(root, "track", "b", "B", "templates/t.html")
    rc, out = run(root, "status")
    check("hygiene: re-tracked name is not reported as failed", "previous build of this page failed" not in out, out)
    rc, out = run(root, "build")
    check("hygiene: re-tracked name builds", rc == 0, out[-200:])
    for op in (("rm", "c"), ("mv", "c", "c2")):
        write(root, "content/c.html", '@input("data/missing.txt")\n')
        run(root, "build")
        check(f"hygiene: {op[0]} precondition", "c" in (root / ".nift/failed.json").read_text())
        rc, out = run(root, *op)
        check(f"hygiene: {op[0]} drops the name", rc == 0 and not (root / ".nift/failed.json").exists(), out)
        if op[0] == "mv":
            run(root, "rm", "c2")
        write(root, "content/c.html", "<p>c</p>\n")
        run(root, "track", "c", "C", "templates/t.html")
        run(root, "build")
    (root / ".nift/failed.json").write_text('{"failed": ["gone-page"]}')
    rc, out = run(root, "build")
    check("hygiene: a no-op build prunes untracked names", rc == 0 and not (root / ".nift/failed.json").exists(), out[-200:])
    same_as_clean(root, scratch, "hygiene")
    shutil.rmtree(root)

def main():
    with tempfile.TemporaryDirectory(prefix="nift-recovery-") as td:
        scratch = Path(td)
        for mode in MODES:
            ordinary_failures(mode, scratch)
            crashes(mode, scratch)
        uncertain_and_concurrency(scratch)
        corrupt_failed_lists(scratch)
        pagination_without_failed_list(scratch)
        symlinked_outputs(scratch)
        failed_list_hygiene(scratch)
        equal_tick_allowance_contract(scratch)
    allowances = (f"; {len(equal_tick_allowances)} confirmed equal-timestamp extra rebuild(s) allowed"
                  + (f" ({', '.join(equal_tick_allowances)})" if equal_tick_allowances else ""))
    if fails:
        print(f"build recovery: {len(fails)} of {checks} checks FAILED{allowances}")
        return 1
    print(f"PASS build recovery (D-020): {checks} checks, modes {','.join(MODES)}{allowances}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
