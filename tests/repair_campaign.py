#!/usr/bin/env python3
"""CP4 repair corruption campaign.

For representative projects, corrupt only DERIVED state, then require:
  build --repair succeeds
  expected output tree converges (paths + bytes)
  ordinary incremental build afterward is clean/successful
  no .unfinished remains
  a second repair converges identically (idempotence)
Also: repair failure on broken authoritative input retains the marker;
interrupted repair retains the marker and a second repair converges;
repair/build concurrency excludes two simultaneous epochs.
"""
import json
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time

NIFT = os.environ.get("NIFT_BIN", str(pathlib.Path(__file__).resolve().parent.parent / "nift"))

fails = []


def check(label, cond):
    print(f"  [{'PASS' if cond else 'FAIL'}] {label}")
    if not cond:
        fails.append(label)


def run(root, *args):
    p = subprocess.run([NIFT, *args], cwd=root, capture_output=True, text=True)
    return p.returncode, p.stderr


def force_write(path, content):
    try:
        os.chmod(path, 0o644)
    except FileNotFoundError:
        pass
    path.write_text(content)


def force_unlink(path):
    try:
        os.chmod(path, 0o644)
    except FileNotFoundError:
        pass
    path.unlink(missing_ok=True)


def marker(root):
    return (root / ".nift/.unfinished").exists()


def tree(root):
    out = {}
    for f in sorted((root / "public").rglob("*")):
        if f.is_file():
            out[str(f.relative_to(root / "public"))] = f.read_bytes()
    return out


def canonical_project(root):
    root.mkdir(exist_ok=True)
    for d in (".nift", "content", "templates", "public"):
        (root / d).mkdir()
    (root / ".nift/config.json").write_text(
        '{"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html",'
        '"build-threads":-1,"incremental-mode":"modified","minify-exts":[]}}')
    (root / ".nift/tracked.json").write_text(json.dumps({
        "tracked": [
            {"name": "/", "title": "Home", "template": "templates/template.html"},
            {"name": "blog", "title": "Blog", "template": "templates/template.html",
             "paginate": {"items-per-page": 1}},
            {"name": "shared", "title": "Shared", "template": "templates/template.html"},
        ]}, separators=(",", ":")))
    (root / "templates/template.html").write_text(
        "<main>@input('templates/head.html')</main>\n@content\n")
    (root / "templates/head.html").write_text("<title>$[title]</title>\n")
    (root / "content/index.html").write_text("<p>home</p>\n")
    (root / "content/blog.html").write_text("@item{one}@item{two}@item{three}@paginate\n")
    (root / "content/blog.paginate.html").write_text("<section>$[paginate.items]</section>\n")
    (root / "content/shared.html").write_text("<p>shared</p>\n")


def repair_and_verify(root, canonical, label):
    rc, _ = run(root, "build", "--repair")
    check(f"{label}: repair succeeds", rc == 0)
    check(f"{label}: marker cleared", not marker(root))
    check(f"{label}: output tree converges", tree(root) == canonical)
    # idempotence: second repair identical
    rc, _ = run(root, "build", "--repair")
    check(f"{label}: second repair succeeds", rc == 0)
    check(f"{label}: second repair identical tree", tree(root) == canonical)
    check(f"{label}: no marker after second repair", not marker(root))
    # ordinary incremental build afterward is clean
    rc, _ = run(root, "build")
    check(f"{label}: ordinary build after repair clean", rc == 0)


def main():
    with tempfile.TemporaryDirectory(prefix="nift-repair-") as td:
        base = pathlib.Path(td)

        # Build a canonical project.
        root = base / "canon"
        canonical_project(root)
        rc, _ = run(root, "build", "--all")
        check("0 canonical build succeeds", rc == 0)
        canonical = tree(root)
        (root / "public/keepme.txt").write_text("USER FILE\n")
        canonical_user = tree(root)
        check("0 user file present", "keepme.txt" in canonical_user)

        cases = []
        for name, mutate in [
            ("delete page output", lambda r: (r / "public/index.html").unlink()),
            ("truncate page output", lambda r: (r / "public/index.html").write_text("<p>home")),
            ("garbage output", lambda r: (r / "public/index.html").write_text("\x00\x01\x02garbage\xff")),
            ("valid-but-wrong output", lambda r: (r / "public/index.html").write_text("<h1>WRONG</h1>\n")),
            ("alter output permissions", lambda r: os.chmod(r / "public/index.html", 0o600)),
            ("partial pagination set", lambda r: (r / "public/blog-3.html").unlink()),
            ("stale pagination surplus", lambda r: (r / "public/blog-9.html").write_text("<section>stale</section>\n")),
            ("corrupt .info.json", lambda r: force_write(r / ".nift/public/blog.info.json", "{{{not json")),
            ("remove .info.json", lambda r: force_unlink(r / ".nift/public/shared.info.json")),
            ("orphan removed page", lambda r: (
                (r / "content/oldpage.html").write_text("<p>old</p>\n"),
                _add_tracked(r, "oldpage"),
                subprocess.run([NIFT, "build", "--all"], cwd=r, stdout=subprocess.DEVNULL),
                _remove_tracked(r, "oldpage"))),
            ("mixed corruption", lambda r: (
                (r / "public/index.html").unlink(),
                (r / "public/blog-2.html").write_text("TORN"),
                (r / ".nift/public/shared.info.json").unlink(),
                (r / "public/blog-8.html").write_text("<section>x</section>\n"))),
        ]:
            cases.append((name, mutate))

        for name, mutate in cases:
            r = base / ("case-" + name.replace(" ", "-"))
            r.mkdir()
            canonical_project(r)
            rc, _ = run(r, "build", "--all")
            (r / "public/keepme.txt").write_text("USER FILE\n")
            mutate(r)
            repair_and_verify(r, canonical_user, name.replace(" ", "-"))
            check(f"{name}: user file preserved", (r / "public/keepme.txt").exists())

        # Pagination shrink with CORRUPT old info (currently still paginated):
        # stale pages beyond the current count must be swept even though the
        # old metadata is unreadable.
        r = base / "pagination-shrink-corrupt"
        r.mkdir()
        canonical_project(r)
        rc, _ = run(r, "build", "--all")
        # items-per-page 1 -> 4: one page only? no: 3 items @4/pp = 1 page.
        # Use 5 items-per-page -> 1 page, currently paginated count==1 -> not
        # swept (limitation). Instead go 1 -> 2 pages would GROW. For a shrink
        # that stays paginated, change items-per-page to 2 (2 pages from 3
        # items), keep stale blog-3.
        d = json.loads((r / ".nift/tracked.json").read_text())
        for t in d["tracked"]:
            if t["name"] == "blog":
                t["paginate"] = {"items-per-page": 2}
        (r / ".nift/tracked.json").write_text(json.dumps(d, separators=(",", ":")))
        force_write(r / ".nift/public/blog.info.json", "corrupt{{{")   # old info unreadable
        (r / "public/blog-3.html").write_text("<section>stale</section>\n")
        rc, _ = run(r, "build", "--repair")
        check("pagination-shrink-corrupt: repair succeeds", rc == 0)
        check("pagination-shrink-corrupt: stale blog-3 removed",
              not (r / "public/blog-3.html").exists())
        check("pagination-shrink-corrupt: no marker", not marker(r))

        # Repair failure on broken authoritative input retains the marker.
        r = base / "repair-failure"
        r.mkdir()
        canonical_project(r)
        rc, _ = run(r, "build", "--all")
        (r / "templates/template.html").write_text("@if(never closed{\n")
        rc, _ = run(r, "build", "--repair")
        check("repair-failure: broken template repair returns nonzero", rc != 0)
        check("repair-failure: marker remains", marker(r))
        (r / "templates/template.html").write_text(
            "<main>@input('templates/head.html')</main>\n@content\n")
        rc, _ = run(r, "build", "--repair")
        check("repair-failure: fixed repair succeeds", rc == 0)
        check("repair-failure: marker cleared", not marker(r))
        check("repair-failure: tree converges", tree(r) == canonical)

        # Interrupted repair: kill mid-epoch -> marker remains -> ordinary
        # refuses -> second repair converges. (Deterministic via the hold hook.)
        r = base / "interrupted-repair"
        r.mkdir()
        canonical_project(r)
        rc, _ = run(r, "build", "--all")
        (r / "public/index.html").unlink()
        hold_dir = base / "hold"
        hold_dir.mkdir()
        hold_env = dict(os.environ, NIFT_TEST_OWNERSHIP_HOLD=str(hold_dir))
        proc = subprocess.Popen([NIFT, "build", "--repair"], cwd=r,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                start_new_session=True, env=hold_env)
        for _ in range(2000):
            if (hold_dir / "acquired").exists():
                break
            time.sleep(0.005)
        os.kill(proc.pid, signal.SIGKILL)
        proc.wait(timeout=60)
        check("interrupted-repair: marker remains after kill", marker(r))
        rc, _ = run(r, "build")
        check("interrupted-repair: ordinary build refuses", rc == 1)
        rc, _ = run(r, "build", "--repair")
        check("interrupted-repair: second repair succeeds", rc == 0)
        check("interrupted-repair: tree converges", tree(r) == canonical)
        check("interrupted-repair: marker cleared", not marker(r))
        shutil.rmtree(hold_dir, ignore_errors=True)

        # Concurrency: repair owns -> build refuses; repair owns -> second
        # repair refuses; build owns -> repair refuses.
        r = base / "repair-concurrency"
        r.mkdir()
        canonical_project(r)
        rc, _ = run(r, "build", "--all")
        hold2 = base / "hold2"
        hold2.mkdir()
        env_hold = dict(os.environ, NIFT_TEST_OWNERSHIP_HOLD=str(hold2))
        proc = subprocess.Popen([NIFT, "build", "--repair"], cwd=r,
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                start_new_session=True, env=env_hold)
        for _ in range(2000):
            if (hold2 / "acquired").exists():
                break
            time.sleep(0.005)
        rc, err = run(r, "build")
        check("repair-concurrency: build refuses while repair owns",
              rc == 1 and "another build appears to be running" in err)
        rc, err = run(r, "build", "--repair")
        check("repair-concurrency: second repair refuses while repair owns",
              rc == 1 and "another build appears to be running" in err)
        (hold2 / "release").touch()
        proc.wait(timeout=60)
        (hold2 / "release").unlink(missing_ok=True)
        check("repair-concurrency: repair completed", proc.returncode == 0)
        # build owns -> repair refuses
        proc = subprocess.Popen([NIFT, "build", "--all"], cwd=r,
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                start_new_session=True, env=env_hold)
        for _ in range(2000):
            if (hold2 / "acquired").exists():
                break
            time.sleep(0.005)
        rc, err = run(r, "build", "--repair")
        check("repair-concurrency: repair refuses while build owns",
              rc == 1 and "another build appears to be running" in err)
        (hold2 / "release").touch()
        proc.wait(timeout=60)
        (hold2 / "release").unlink(missing_ok=True)
        shutil.rmtree(hold2, ignore_errors=True)
        check("repair-concurrency: no marker after both complete", not marker(r))

    if fails:
        print(f"\nFAILED: {len(fails)}: {fails}")
        sys.exit(1)
    print("\nALL REPAIR CAMPAIGN TESTS PASSED")


def _add_tracked(root, name):
    d = json.loads((root / ".nift/tracked.json").read_text())
    d["tracked"].append({"name": name, "title": name, "template": "templates/template.html"})
    (root / ".nift/tracked.json").write_text(json.dumps(d, separators=(",", ":")))


def _remove_tracked(root, name):
    d = json.loads((root / ".nift/tracked.json").read_text())
    d["tracked"] = [t for t in d["tracked"] if t["name"] != name]
    (root / ".nift/tracked.json").write_text(json.dumps(d, separators=(",", ":")))


if __name__ == "__main__":
    main()
