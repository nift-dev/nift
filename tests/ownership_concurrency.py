#!/usr/bin/env python3
"""CP2 ownership/lock + .unfinished protocol tests.

Deterministically drives the acquisition protocol against the real `nift`
binary, simulating the other side of the race with a Python-held advisory
lock (flock) on the marker file. The kernel guarantees mutual exclusion, so a
Python-held flock exercises exactly the same C++ flock(LOCK_EX|LOCK_NB) code
path as a second Nift process. A two-process stress loop additionally proves
that concurrent real builds never corrupt state or hang.

Protocol under test (ProjectOwnership::acquire):
  Clean  -> caller created the marker; normal build may proceed
  Stale  -> marker exists, no live owner; normal build refuses, --repair may
  Live   -> a live process owns the lock; everyone refuses
"""
import fcntl
import json
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time

NIFT = os.environ.get("NIFT_BIN", str(pathlib.Path(__file__).resolve().parent.parent / "nift"))


def scaffold(root: pathlib.Path, pages: int = 8):
    for d in (".nift", "content", "templates", "public"):
        (root / d).mkdir(exist_ok=True)
    (root / ".nift/config.json").write_text(
        '{"config":{"content-dir":"content/","output-dir":"public/",'
        '"default-template":"templates/template.html","build-threads":-1,'
        '"incremental-mode":"modified","minify-exts":[]}}')
    (root / ".nift/tracked.json").write_text(json.dumps(
        {"tracked": [{"name": f"p{i}", "title": f"P{i}",
                      "template": "templates/template.html"} for i in range(pages)]},
        separators=(",", ":")))
    (root / "templates/template.html").write_text("@content\n")
    for i in range(pages):
        (root / "content" / f"p{i}.html").write_text(f"<p>{i}</p>\n")


def run(root, *args, env=None):
    p = subprocess.run([NIFT, *args], cwd=root, capture_output=True, text=True, env=env)
    return p.returncode, p.stderr


def hold_lock(marker, create=False):
    """Open the marker and take an exclusive advisory lock (live-build side)."""
    if create:
        open(marker, "w").close()
    fd = os.open(marker, os.O_RDWR)
    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    return fd


fails = []


def check(label, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {label}" + (f"  ({detail})" if detail and not cond else ""))
    if not cond:
        fails.append(label)


def main():
    with tempfile.TemporaryDirectory(prefix="nift-own-") as td:
        root = pathlib.Path(td)

        # 1. Clean lifecycle: build -> marker created during epoch, absent after.
        scaffold(root)
        rc, _ = run(root, "build", "--all")
        check("1 clean build succeeds", rc == 0)
        check("1 marker absent after successful build", not (root / ".nift/.unfinished").exists())

        # 2. Pre-mutation validation failure leaves no marker.
        (root / ".nift/tracked.json").write_text("{{{ not json")
        rc, _ = run(root, "build", "--all")
        check("2 malformed tracked.json build fails", rc != 0)
        check("2 no marker after pre-mutation validation failure", not (root / ".nift/.unfinished").exists())
        scaffold(root)
        run(root, "build", "--all")

        # 3. Failed post-mutation build leaves the marker (stale).
        (root / "content" / "p0.html").unlink()   # per-page failure after acquisition
        rc, _ = run(root, "build", "--all")
        check("3 failing build returns nonzero", rc != 0)
        check("3 marker remains after failed post-mutation build", (root / ".nift/.unfinished").exists())
        (root / "content" / "p0.html").write_text("<p>ok</p>\n")

        # 4. Stale marker + ordinary build refuses; --repair recovers.
        rc, err = run(root, "build")
        check("4 ordinary build refuses on stale marker", rc == 1)
        check("4 refusal directs toward build --repair", "build --repair" in err)
        rc, err = run(root, "build", "--all")
        check("4 build --all refuses on stale marker", rc == 1 and "build --repair" in err)
        rc, err = run(root, "build", "/")
        check("4 targeted build refuses on stale marker", rc == 1)
        rc, _ = run(root, "build", "--repair")
        check("4 build --repair succeeds on stale marker", rc == 0)
        check("4 marker removed after successful repair", not (root / ".nift/.unfinished").exists())

        # 5. Live lock (Python holds flock) -> every mutator refuses; info/status read.
        fd = hold_lock(root / ".nift/.unfinished", create=True)
        for label, args in [
            ("build", ["build"]), ("build --all", ["build", "--all"]),
            ("build --repair", ["build", "--repair"]), ("build p0", ["build", "p0"]),
            ("untrack", ["untrack", "p0"]), ("cp", ["cp", "p0", "p9"]), ("mv", ["mv", "p0", "p9"]),
        ]:
            rc, err = run(root, *args)
            check(f"5 {label} refuses on live lock", rc == 1 and "another build appears to be running" in err)
        rc, _ = run(root, "info", "--names")
        check("5 info --names is read-only and works", rc == 0)
        rc, _ = run(root, "status")
        check("5 status is read-only and works", rc == 0)
        fcntl.flock(fd, fcntl.LOCK_UN)
        os.close(fd)

        # 6. Process killed while holding the lock: lock released by the kernel,
        #    marker survives, ordinary build refuses, repair acquires.
        child = os.fork()
        if child == 0:
            fd2 = hold_lock(root / ".nift/.unfinished", create=True)
            os.write(fd2, b"")
            time.sleep(30)
            os._exit(0)
        time.sleep(0.4)
        rc, err = run(root, "build")
        check("6 ordinary build refuses while child holds live lock", rc == 1 and "another build appears" in err)
        os.kill(child, signal.SIGKILL)
        os.waitpid(child, 0)
        check("6 marker survives SIGKILL", (root / ".nift/.unfinished").exists())
        rc, _ = run(root, "build")
        check("6 ordinary build refuses after SIGKILL (stale)", rc == 1 and "unfinished build detected" in run(root, "build")[1])
        rc, _ = run(root, "build", "--repair")
        check("6 repair acquires after SIGKILL", rc == 0)
        check("6 marker removed after repair", not (root / ".nift/.unfinished").exists())

        # 7. Two-process stress: concurrent real builds never corrupt or hang.
        big = pathlib.Path(td) / "big"
        big.mkdir()
        scaffold(big, pages=2000)
        run(big, "build", "--all")
        reference = {f.name: f.read_bytes() for f in (big / "public").glob("*.html")}
        concurrent_ok = True
        for i in range(12):
            procs = [subprocess.Popen([NIFT, "build", "--all"], cwd=big,
                                      stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                     for _ in range(2)]
            codes = [p.wait() for p in procs]
            stderrs = [p.stderr.read().decode(errors="replace") for p in procs]
            if not all(c in (0, 1) for c in codes):
                concurrent_ok = False
                break
            if codes.count(0) < 1:
                concurrent_ok = False
                break
            # Any refusal must be a live-lock refusal, not corruption.
            for code, err in zip(codes, stderrs):
                if code == 1 and "another build appears to be running" not in err:
                    concurrent_ok = False
                    break
            if not concurrent_ok:
                break
        check("7 concurrent builds: >=1 succeeds and refusals are live-lock", concurrent_ok)
        after = {f.name: f.read_bytes() for f in (big / "public").glob("*.html")}
        check("7 concurrent builds leave byte-identical outputs", after == reference)
        check("7 no marker left after all concurrent builds completed", not (big / ".nift/.unfinished").exists())

    if fails:
        print(f"\nFAILED: {len(fails)}: {fails}")
        sys.exit(1)
    print("\nALL OWNERSHIP/CONCURRENCY TESTS PASSED")


if __name__ == "__main__":
    main()
