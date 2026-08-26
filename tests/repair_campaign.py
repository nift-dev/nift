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
                subprocess.run([NIFT, "build", "--all"], cwd=r, stdout=subprocess.DEVNULL, check=True),
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
            if name == "orphan removed page":
                # Conservative orphan-output rule: the orphan .info.json is
                # removed, but the historical public output is PRESERVED (its
                # path is only knowable from distrustable derived metadata).
                rc, _ = run(r, "build", "--repair")
                check("orphan: repair succeeds", rc == 0)
                check("orphan: marker cleared", not marker(r))
                check("orphan: orphan .info.json removed",
                      not (r / ".nift/public/oldpage.info.json").exists())
                check("orphan: historical output preserved (limitation)",
                      (r / "public/oldpage.html").exists())
                check("orphan: user file preserved", (r / "public/keepme.txt").exists())
                rc, _ = run(r, "build")
                check("orphan: ordinary build after repair clean", rc == 0)
                continue
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

        # Hostile-metadata protection: a corrupt-but-valid orphan .info.json
        # must NOT authorize deleting a public file. The orphan sweep now
        # deletes only the metadata itself; public outputs are preserved unless
        # ownership is established independently (never from derived metadata).
        hostile_targets = [
            ("hostile-output-path", '{"output": "public/keepme.txt"}', "public/keepme.txt"),
            ("hostile-traversal", '{"output": "../outside.txt"}', None),
            ("hostile-absolute", '{"output": "/tmp/nift-hostile-abs"}', None),
            ("hostile-corrupt-guess", "not-json{{{{", "public/evil.html"),
            ("hostile-custom-ext", '{"output": "public/evil.php"}', "public/evil.html"),
        ]
        for hname, info_content, user_file in hostile_targets:
            r = base / hname
            r.mkdir()
            canonical_project(r)
            rc, _ = run(r, "build", "--all")
            # a "never-tracked" page name -> its .info.json is an orphan
            force_write(r / ".nift/public/evil.info.json", info_content)
            if user_file:
                (r / user_file).write_text("USER DATA\n")
            rc, _ = run(r, "build", "--repair")
            check(f"{hname}: repair succeeds", rc == 0)
            check(f"{hname}: marker cleared", not marker(r))
            check(f"{hname}: orphan .info.json removed",
                  not (r / ".nift/public/evil.info.json").exists())
            if user_file:
                check(f"{hname}: user file preserved", (r / user_file).exists())
            check(f"{hname}: no output beyond project deleted",
                  not (r / "keepme.txt").exists() and not (r.parent / "outside.txt").exists())

        # Sweep-failure: a REQUIRED orphan-info removal fails (read-only
        # containing dir) -> repair non-zero, marker retained, ordinary build
        # refuses; restore -> second repair succeeds. (Permission-based; root
        # bypasses POSIX mode bits, so it is skipped when running as root.)
        if os.geteuid() != 0:
            r = base / "sweep-failure"
            r.mkdir()
            canonical_project(r)
            (r / "content/gone").mkdir()
            (r / "content/gone/sub.html").write_text("<p>gone</p>\n")
            _add_tracked(r, "gone/sub")
            rc, _ = run(r, "build", "--all")
            check("sweep-failure: initial build succeeds", rc == 0)
            _remove_tracked(r, "gone/sub")
            (r / ".nift/public/gone").chmod(0o555)  # blocks orphan-info removal
            rc, _ = run(r, "build", "--repair")
            check("sweep-failure: repair returns non-zero", rc != 0)
            check("sweep-failure: marker retained", marker(r))
            rc, _ = run(r, "build")
            check("sweep-failure: ordinary build refuses", rc == 1)
            (r / ".nift/public/gone").chmod(0o755)
            rc, _ = run(r, "build", "--repair")
            check("sweep-failure: second repair succeeds", rc == 0)
            check("sweep-failure: marker cleared", not marker(r))
            check("sweep-failure: orphan info removed",
                  not (r / ".nift/public/gone/sub.info.json").exists())

        # Hash-mode: stored hashes are regenerable cache. Repair removes an
        # orphaned hash (mirrored source gone) and keeps valid ones. The @dep
        # reference is removed at the same time as the source so the page still
        # builds (deleting a still-referenced dependency is a repair failure).
        r = base / "hash-mode"
        r.mkdir()
        for d in (".nift", "content", "templates", "public", "data"):
            (r / d).mkdir()
        (r / ".nift/config.json").write_text(
            '{"config":{"content-dir":"content/","output-dir":"public/",'
            '"default-template":"templates/template.html","build-threads":-1,'
            '"incremental-mode":"hash"}}')
        (r / ".nift/tracked.json").write_text(
            '{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}')
        (r / "templates/template.html").write_text("@content\\n@dep('data/state.json')\\n")
        (r / "content/index.html").write_text("<p>home</p>\n")
        (r / "data/state.json").write_text("{}\\n")
        rc, _ = run(r, "build", "--all")
        check("hash-mode: initial build succeeds", rc == 0)
        check("hash-mode: hashes written",
              (r / ".nift/content/index.html.hash").exists()
              and (r / ".nift/data/state.json.hash").exists())
        (r / "data/state.json").unlink()                     # orphan the state hash
        (r / "templates/template.html").write_text("@content\\n")
        rc, _ = run(r, "build", "--repair")
        check("hash-mode: repair succeeds", rc == 0)
        check("hash-mode: orphaned data hash removed",
              not (r / ".nift/data/state.json.hash").exists())
        check("hash-mode: valid content hash kept",
              (r / ".nift/content/index.html.hash").exists())

        # Pagination ownership predicate: only <base>-<N> with N >= 2 is in the
        # owned namespace. -0/-1 and leading-zero-below-2 / overflow suffixes
        # are user files and must be preserved; stale N>=2 surplus is removed;
        # current members and a tracked page whose PRIMARY collides with the
        # namespace are preserved.
        r = base / "pagination-ownership"
        r.mkdir()
        canonical_project(r)
        rc, _ = run(r, "build", "--all")
        user = {
            "blog-0.html": "USER-0",
            "blog-1.html": "USER-1",
            "blog-00.html": "USER-00",
            "blog-01.html": "USER-01",
            "blog-0001.html": "USER-0001",
            # zero-padded but numerically >= 2: NOT canonical Nift paths (Nift
            # emits unpadded std::to_string), so they are user files
            "blog-02.html": "USER-02",
            "blog-03.html": "USER-03",
            "blog-0002.html": "USER-0002",
            "blog-0003.html": "USER-0003",
            "blog-0009.html": "USER-0009",
            "blog-99999999999999999999.html": "USER-BIG",   # overflows uint64
        }
        for name, content in user.items():
            (r / "public" / name).write_text(content)
        (r / "public/blog-9.html").write_text("<section>stale</section>\n")
        rc, _ = run(r, "build", "--repair")
        check("pagination-ownership: repair succeeds", rc == 0)
        for name, content in user.items():
            check(f"pagination-ownership: {name} preserved byte-for-byte",
                  (r / "public" / name).read_text() == content)
        check("pagination-ownership: current page-2 correct",
              (r / "public/blog-2.html").read_text()
              == "<main><title>Blog</title></main>\n<section>two</section>\n\n")
        check("pagination-ownership: current page-3 correct",
              (r / "public/blog-3.html").read_text()
              == "<main><title>Blog</title></main>\n<section>three</section>\n\n")
        check("pagination-ownership: stale N>=2 surplus removed",
              not (r / "public/blog-9.html").exists())
        check("pagination-ownership: no marker", not marker(r))

        # Tracked primary collision: a tracked page literally named "blog-5"
        # owns public/blog-5.html, which lies inside blog's pagination
        # namespace. The current_owned exemption must preserve it.
        r = base / "pagination-primary-collision"
        r.mkdir()
        canonical_project(r)
        (r / "content/blog-5.html").write_text("<p>a real page named blog-5</p>\n")
        _add_tracked(r, "blog-5")
        rc, _ = run(r, "build", "--all")
        check("pagination-primary-collision: initial build succeeds", rc == 0)
        check("pagination-primary-collision: blog-5 page output exists",
              (r / "public/blog-5.html").exists())
        (r / "public/blog-9.html").write_text("<section>stale</section>\n")
        rc, _ = run(r, "build", "--repair")
        check("pagination-primary-collision: repair succeeds", rc == 0)
        check("pagination-primary-collision: tracked primary blog-5 preserved",
              (r / "public/blog-5.html").read_text()
              == "<main><title>blog-5</title></main>\n<p>a real page named blog-5</p>\n")
        check("pagination-primary-collision: stale surplus removed",
              not (r / "public/blog-9.html").exists())

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
