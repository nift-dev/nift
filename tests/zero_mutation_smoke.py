#!/usr/bin/env python3
"""CP2.2 zero-mutation failure distinction tests.

Governing invariant:
  .unfinished survives  iff  Nift has evidence derived state may be incomplete
  (not merely that a build returned non-zero).

  success -> remove
  controlled failure + mutation_started==false -> remove
  controlled failure + mutation_started==true  -> retain
  crash/SIGKILL                                -> retain
  build --repair failure (even zero new mutations) -> retain
"""
import json
import shutil
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time

NIFT = os.environ.get("NIFT_BIN", str(pathlib.Path(__file__).resolve().parent.parent / "nift"))


def scaffold(root, pages, broken_index=None, templates=None):
    """pages: list of page names. templates: {name: content}. Page 'pN' uses
    templates/t{broken_index} if broken_index is set and N==broken_index,
    else the default template."""
    for d in (".nift", "content", "templates", "public"):
        (root / d).mkdir(exist_ok=True)
    (root / ".nift/config.json").write_text(
        '{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/",'
        '"output-ext":".html","default-template":"templates/template.html",'
        '"build-threads":-1,"incremental-mode":"modified","minify-exts":[]}}')
    tracked = []
    for i, name in enumerate(pages):
        t = f"templates/t{i}.html" if broken_index == i else "templates/template.html"
        tracked.append({"name": name, "title": name, "template": t})
    (root / ".nift/tracked.json").write_text(json.dumps({"tracked": tracked}, separators=(",", ":")))
    (root / "templates/template.html").write_text("@content\n")
    for i in range(len(pages)):
        (root / f"templates/t{i}.html").write_text("@content\n")
    for name in pages:
        stem = "index" if name == "/" else name
        (root / "content" / f"{stem}.html").write_text(f"<p>{name}</p>\n")


def run(root, *args):
    p = subprocess.run([NIFT, *args], cwd=root, capture_output=True, text=True)
    return p.returncode, p.stderr


def marker(root):
    return (root / ".nift/.unfinished").exists()


fails = []


def check(label, cond):
    print(f"  [{'PASS' if cond else 'FAIL'}] {label}")
    if not cond:
        fails.append(label)


def main():
    with tempfile.TemporaryDirectory(prefix="nift-zeromut-") as td:
        base = pathlib.Path(td)

        # 1. All-pages render failure before any write -> marker cleared.
        root = base / "t1"
        root.mkdir()
        scaffold(root, ["/"])
        (root / "templates/template.html").write_text("@if(never closed{\n")
        rc, _ = run(root, "build")
        check("1 all-pages render failure returns nonzero", rc != 0)
        check("1 marker cleared (zero mutations)", not marker(root))
        (root / "templates/template.html").write_text("@content\n")
        rc, _ = run(root, "build")
        check("1 fix + ordinary build succeeds", rc == 0)

        # 2. Schema/parser pre-write failure -> marker cleared.
        root = base / "t2"
        root.mkdir()
        for d in (".nift", "content", "templates", "public", "data", "schemas"):
            (root / d).mkdir()
        (root / ".nift/config.json").write_text(
            '{"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html",'
            '"build-threads":-1,"incremental-mode":"modified"}}')
        (root / ".nift/tracked.json").write_text(
            '{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}')
        (root / "templates/template.html").write_text(
            '@json(data, "schemas/items.schema.json", "data/items.json")\n@content\n')
        (root / "data/items.json").write_text('{"items":[{"name":"oops","rank":"2"}]}')
        (root / "schemas/items.schema.json").write_text(
            '{"type":"object","properties":{"items":{"type":"array","items":{"type":"object",'
            '"required":["name","rank"],"properties":{"rank":{"type":"integer"}}}}}}')
        (root / "content/index.html").write_text("\n")
        rc, _ = run(root, "build")
        check("2 schema failure returns nonzero", rc != 0)
        check("2 marker cleared (schema error precedes any write)", not marker(root))
        (root / "data/items.json").write_text('{"items":[{"name":"ok","rank":1}]}')
        rc, _ = run(root, "build")
        check("2 fix + ordinary build succeeds", rc == 0)

        # 3. One page writes, another fails -> marker REMAINS; --repair recovers.
        root = base / "t3"
        root.mkdir()
        scaffold(root, ["a", "b"], broken_index=1)
        (root / "templates/t1.html").write_text("@if(never closed{\n")
        rc, _ = run(root, "build", "--all")
        check("3 mixed success/failure build returns nonzero", rc != 0)
        check("3 marker REMAINS (a page wrote, b failed)", marker(root))
        rc, err = run(root, "build")
        check("3 ordinary build refuses", rc == 1 and "build --repair" in err)
        (root / "templates/t1.html").write_text("@content\n")
        rc, _ = run(root, "build", "--repair")
        check("3 build --repair succeeds", rc == 0)
        check("3 marker cleared after repair", not marker(root))

        # 4. Stale pagination deletion happened, then another page failed ->
        #    marker REMAINS. Page x shrinks pagination (stale outputs deleted),
        #    page y renders and fails.
        root = base / "t4"
        root.mkdir()
        scaffold(root, ["x", "y"])
        (root / "content/x.html").write_text("@item{one}@item{two}@item{three}@paginate\n")
        (root / "content/x.paginate.html").write_text("<section>$[paginate.items]</section>\n")
        (root / ".nift/tracked.json").write_text(json.dumps({
            "tracked": [
                {"name": "x", "title": "X", "template": "templates/template.html",
                 "paginate": {"items-per-page": 1}},
                {"name": "y", "title": "Y", "template": "templates/t1.html"},
            ]}, separators=(",", ":")))
        (root / "templates/t1.html").write_text("@content\n")
        rc, _ = run(root, "build", "--all")
        check("4 initial paginated build succeeds", rc == 0)
        check("4 page-3 exists", (root / "public/x-3.html").exists())
        # Shrink to one page AND break y's template: x's stale outputs get
        # deleted, then y fails -> derived mutation happened.
        (root / ".nift/tracked.json").write_text(json.dumps({
            "tracked": [
                {"name": "x", "title": "X", "template": "templates/template.html",
                 "paginate": {"items-per-page": 3}},
                {"name": "y", "title": "Y", "template": "templates/t1.html"},
            ]}, separators=(",", ":")))
        (root / "templates/t1.html").write_text("@if(never closed{\n")
        rc, _ = run(root, "build", "--all")
        check("4 shrink+break build returns nonzero", rc != 0)
        check("4 marker REMAINS (stale deletion + partial writes)", marker(root))
        (root / "templates/t1.html").write_text("@content\n")
        rc, _ = run(root, "build", "--repair")
        check("4 build --repair succeeds", rc == 0)
        check("4 stale page-3 gone after repair", not (root / "public/x-3.html").exists())
        check("4 marker cleared", not marker(root))

        # 5. SIGKILL while holding ownership (mid-epoch) -> marker REMAINS;
        #    --repair recovers. Uses the env-gated hold hook so the build is
        #    deterministically inside its ownership epoch (marker present,
        #    lock held) when killed.
        root = base / "t5"
        root.mkdir()
        scaffold(root, [f"p{i}" for i in range(8)])
        rc, _ = run(root, "build", "--all")
        check("5 initial build succeeds", rc == 0)
        hold_dir = root.parent / "hold5"
        hold_dir.mkdir()
        hold_env = dict(os.environ, NIFT_TEST_OWNERSHIP_HOLD=str(hold_dir))
        proc = subprocess.Popen([NIFT, "build", "--all"], cwd=root,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                start_new_session=True, env=hold_env)
        for _ in range(2000):
            if (hold_dir / "acquired").exists():
                break
            time.sleep(0.005)
        check("5 build reached ownership epoch", (hold_dir / "acquired").exists())
        os.kill(proc.pid, signal.SIGKILL)
        proc.wait(timeout=60)
        check("5 marker REMAINS after SIGKILL mid-epoch", marker(root))
        rc, _ = run(root, "build")
        check("5 ordinary build refuses after kill", rc == 1)
        rc, _ = run(root, "build", "--repair")
        check("5 build --repair succeeds after kill", rc == 0)
        check("5 marker cleared after repair", not marker(root))
        shutil.rmtree(hold_dir, ignore_errors=True)

        # 6. build --repair fails before new mutations -> marker STILL remains.
        root = base / "t6"
        root.mkdir()
        scaffold(root, ["/"])
        (root / "templates/template.html").write_text("@if(never closed{\n")
        (root / ".nift/.unfinished").touch()          # stale evidence
        rc, _ = run(root, "build", "--repair")
        check("6 repair fails on broken source", rc != 0)
        check("6 marker REMAINS after failed repair (evidence unresolved)",
              marker(root))
        (root / "templates/template.html").write_text("@content\n")
        rc, _ = run(root, "build", "--repair")
        check("6 fixed repair succeeds", rc == 0)
        check("6 marker cleared after successful repair", not marker(root))

        # 7. build --auto pass fails with zero mutation -> marker cleared; the
        #    watch loop still exits non-zero (conservative error handling).
        root = base / "t7"
        root.mkdir()
        scaffold(root, ["/"])
        (root / "templates/template.html").write_text("@if(never closed{\n")
        p = subprocess.run([NIFT, "build", "--auto"], cwd=root, capture_output=True,
                           text=True, timeout=60)
        check("7 build --auto exits non-zero on first failed pass", p.returncode != 0)
        check("7 marker cleared (pass made zero mutations)", not marker(root))
        (root / "templates/template.html").write_text("@content\n")
        rc, _ = run(root, "build")
        check("7 ordinary build succeeds after fix", rc == 0)

        # 8. Targeted duplicate-name rejection: no derived mutation, no marker.
        root = base / "t8"
        root.mkdir()
        scaffold(root, ["/"])
        rc, _ = run(root, "build", "/", "/")
        check("8 duplicate-name rejection returns nonzero", rc != 0)
        check("8 no marker after duplicate-name rejection", not marker(root))

        # 9. Targeted build with no resolvable jobs: no marker.
        rc, _ = run(root, "build", "bogus")
        check("9 no-resolvable-jobs returns nonzero", rc != 0)
        check("9 no marker after empty targeted build", not marker(root))

        # 10. reconcile_watch controlled failure BEFORE derived deletion
        #     (corrupt watch state): zero mutations, marker cleared.
        root = base / "t10"
        root.mkdir()
        scaffold(root, ["/"])
        (root / "content/posts").mkdir(exist_ok=True)
        (root / "content/posts/p1.html").write_text("<p>post</p>\n")
        rc, _ = run(root, "watch", "content/posts", ".html")
        check("10 watch setup succeeds", rc == 0)
        rc, _ = run(root, "build", "--all")
        check("10 initial watch build succeeds", rc == 0)
        state = root / ".nift/.watch/content/posts/tracked.json"
        check("10 watch state created", state.exists())
        state.write_text("not-json{{{")
        rc, _ = run(root, "build", "--all")
        check("10 corrupt watch state build returns nonzero", rc != 0)
        check("10 marker cleared (reconcile failed before any derived deletion)",
              not marker(root))
        check("10 watched page output intact", (root / "public/posts/p1.html").exists())

        # 11. reconcile_watch failure AFTER derived deletion (output removed for
        #     a disappeared page, then the watch-state save fails): marker
        #     REMAINS; ordinary build refuses; --repair recovers once the
        #     authoritative project is restored.
        root = base / "t11"
        root.mkdir()
        scaffold(root, ["/"])
        (root / "content/posts").mkdir(exist_ok=True)
        (root / "content/posts/p1.html").write_text("<p>post</p>\n")
        rc, _ = run(root, "watch", "content/posts", ".html")
        check("11 watch setup succeeds", rc == 0)
        rc, _ = run(root, "build", "--all")
        check("11 initial watch build succeeds", rc == 0)
        state_dir = root / ".nift/.watch/content/posts"
        check("11 watch state dir created", state_dir.is_dir())
        (root / "content/posts/p1.html").rename(root / "content/posts/p1.bak")
        os.chmod(state_dir, 0o555)          # make the watch-state save fail
        rc, _ = run(root, "build", "--all")
        check("11 failed-reconcile build returns nonzero", rc != 0)
        check("11 derived deletion happened (p1 output removed)",
              not (root / "public/posts/p1.html").exists())
        check("11 marker REMAINS (mutation_started true)", marker(root))
        rc, err = run(root, "build")
        check("11 ordinary build refuses", rc == 1 and "build --repair" in err)
        os.chmod(state_dir, 0o755)          # restore watch-state writability
        (root / "content/posts/p1.bak").rename(root / "content/posts/p1.html")
        rc, _ = run(root, "build", "--repair")
        check("11 build --repair recovers", rc == 0)
        check("11 marker cleared after repair", not marker(root))

        # 12-16. Watched targeted-build regressions (CP3.2): TrackedInfo*
        # pointers must be resolved ONLY after reconcile_watch (which can
        # structurally modify `tracked`), so targeted builds survive vector
        # reallocation and preserve discovery/removal semantics.

        def watch_project(tag):
            root = base / tag
            root.mkdir()
            scaffold(root, ["/"])
            (root / "content/posts").mkdir(exist_ok=True)
            return root

        # 12. Reallocation: reconcile ADDS many pages (reallocates tracked);
        #     an existing requested page must still build correctly. (Catches a
        #     dangling TrackedInfo* under sanitizer builds.)
        root = watch_project("t12")
        rc, _ = run(root, "watch", "content/posts", ".html")
        check("12 watch setup succeeds", rc == 0)
        for i in range(200):
            (root / "content/posts" / f"p{i}.html").write_text(f"<p>{i}</p>\n")
        rc, _ = run(root, "build", "/")
        check("12 existing page builds after tracked reallocation", rc == 0)
        check("12 / output correct", (root / "public/index.html").read_text() == "<p>/</p>\n")

        # 13. Newly-discoverable target: reconcile discovers a watched page and
        #     the targeted build then resolves and builds it.
        root = watch_project("t13")
        rc, _ = run(root, "watch", "content/posts", ".html")
        check("13 watch setup succeeds", rc == 0)
        (root / "content/posts/new.html").write_text("<p>new</p>\n")
        rc, _ = run(root, "build", "posts/new")
        check("13 newly-discovered page builds", rc == 0)
        check("13 output exists", (root / "public/posts/new.html").exists())

        # 14. Disappearing watched target: reconcile removes it (and deletes its
        #     output = derived mutation) before targeted resolution; the result
        #     is a clean failure with the marker retained, never a stale-pointer
        #     dereference.
        root = watch_project("t14")
        rc, _ = run(root, "watch", "content/posts", ".html")
        check("14 watch setup succeeds", rc == 0)
        (root / "content/posts/p1.html").write_text("<p>one</p>\n")
        rc, _ = run(root, "build", "--all")
        check("14 initial build succeeds", rc == 0)
        (root / "content/posts/p1.html").unlink()
        rc, _ = run(root, "build", "posts/p1")
        check("14 disappearing-target build fails cleanly", rc != 0)
        check("14 marker retained (reconcile deleted derived output)",
              marker(root))
        rc, _ = run(root, "build", "--repair")
        check("14 --repair recovers", rc == 0)
        check("14 marker cleared after repair", not marker(root))

        # 15. Empty resolved job set, reconcile made ZERO derived mutations ->
        #     no marker.
        root = watch_project("t15")
        rc, _ = run(root, "watch", "content/posts", ".html")
        check("15 watch setup succeeds", rc == 0)
        rc, _ = run(root, "build", "--all")
        check("15 initial build succeeds", rc == 0)
        rc, _ = run(root, "build", "never-existed")
        check("15 unresolvable target fails", rc != 0)
        check("15 no marker (zero mutations)", not marker(root))

        # 16. Empty resolved job set while reconcile DID perform a derived
        #     deletion -> marker retained.
        root = watch_project("t16")
        rc, _ = run(root, "watch", "content/posts", ".html")
        check("16 watch setup succeeds", rc == 0)
        (root / "content/posts/p1.html").write_text("<p>one</p>\n")
        rc, _ = run(root, "build", "--all")
        check("16 initial build succeeds", rc == 0)
        (root / "content/posts/p1.html").unlink()   # reconcile will delete output
        rc, _ = run(root, "build", "never-existed")  # no jobs, but reconcile mutated
        check("16 unresolvable target fails", rc != 0)
        check("16 marker retained (reconcile performed a derived deletion)",
              marker(root))
        rc, _ = run(root, "build", "--repair")
        check("16 --repair recovers", rc == 0)
        check("16 marker cleared after repair", not marker(root))

    if fails:
        print(f"\nFAILED: {len(fails)}: {fails}")
        sys.exit(1)
    print("\nALL ZERO-MUTATION FAILURE TESTS PASSED")


if __name__ == "__main__":
    main()
