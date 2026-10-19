#!/usr/bin/env python3
"""feature-host-deps (D-028): every filesystem read that influences a page is
a build dependency, whichever API performs it.

Incremental-versus-clean differential test, in modified, hash and hybrid
modes. One page reads files through open(), open_bytes(), file().open("r"), page(x).content and
page(x).metadata,
file().open("rw"), ifstream read_line/read_all/extraction (>>), external
files (absolute path, parent traversal), symlinks to external targets,
missing files (absence) and permission-denied files (unreadable). After every
edit an incremental `nift build` must produce exactly the output of a clean
build of a fresh copy of the authored inputs, and the following `nift status`
must report the page up to date (no perpetual rebuild). A separate scenario
edits a file while the page is part-way through streaming it.

On the pre-feature binary the open_bytes, ifstream, rw, missing-file and
permission steps fail (stale output); they pass with the fix.

Usage: NIFT_BIN=/path/to/nift python3 tests/runtime2/host_deps.py [--keep]
"""
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import hashlib
import json
import re
import resource
import time
from pathlib import Path

NIFT = os.path.abspath(os.environ.get("NIFT_BIN") or "./nift")
KEEP = "--keep" in sys.argv
MODES = ["modified", "hash", "hybrid"]

# Root reads chmod-000 files anyway; drop the DAC override capabilities for
# every nift invocation so permission failures are real (as cp16 fs_eacces).
PRIV = []
PERMISSIONS = True
if os.geteuid() == 0:
    if sys.platform.startswith("linux") and shutil.which("setpriv"):
        PRIV = ["setpriv", "--bounding-set", "-dac_override,-dac_read_search"]
    else:
        PERMISSIONS = False

failures = []
UP_TO_DATE = re.compile(r"all \d+ tracked files? (is|are) up to date")


def bounded():
    # Standing rule: every nift run is bounded in disk, memory and time.
    for kind, value in ((resource.RLIMIT_FSIZE, 256 << 20), (resource.RLIMIT_AS, 4 << 30)):
        hard = resource.getrlimit(kind)[1]
        if hard != resource.RLIM_INFINITY:
            value = min(value, hard)
        resource.setrlimit(kind, (value, value))


def nift(cwd, *args, env=None, check=True):
    proc = subprocess.run(PRIV + [NIFT, *args], cwd=cwd, capture_output=True, text=True, timeout=120, env=env,
                          preexec_fn=bounded)
    if check and proc.returncode != 0:
        raise RuntimeError("nift %s failed in %s:\n%s%s" % (" ".join(args), cwd, proc.stdout, proc.stderr))
    return proc


def page_lines(root):
    text = (root / "public" / "index.html").read_text(errors="replace")
    return [line for line in text.splitlines() if line[:1].isupper() and ":" in line[:6]]


def tree(root):
    out = {}
    # Tracked page outputs (`nift init` also seeds public/assets, which is
    # authored output, not built) and the page's copy destinations.
    # Copy destinations written by the page (cp()/file().copy()) count as output.
    for path in sorted(list((root / "public").rglob("*.html")) + list((root / "copies").glob("*"))):
        if path.is_file():
            out[str(path.relative_to(root))] = path.read_bytes()
    return out


def clean_oracle(root, oracle):
    if oracle.exists():
        shutil.rmtree(oracle)
    oracle.mkdir()
    for path in root.iterdir():
        if path.name in ("public", ".nift", "copies"):
            continue
        dest = oracle / path.name
        if path.is_symlink():
            dest.symlink_to(os.readlink(path))
        elif path.is_dir():
            shutil.copytree(path, dest, symlinks=True)
        else:
            shutil.copy2(path, dest)
    (oracle / ".nift").mkdir()
    for name in ("config.json", "tracked.json"):
        shutil.copy2(root / ".nift" / name, oracle / ".nift" / name)
    nift(oracle, "build", "--all")
    return tree(oracle)


def compare(mode, step, root, oracle):
    nift(root, "build")
    incremental = tree(root)
    expected = clean_oracle(root, oracle)
    if incremental == expected:
        print("PASS [%s] %s: incremental == clean" % (mode, step))
    else:
        failures.append((mode, step))
        print("FAIL [%s] %s: incremental build differs from a clean build" % (mode, step))
        for key in sorted(set(incremental) | set(expected)):
            if incremental.get(key) != expected.get(key):
                mine = incremental.get(key, b"").decode(errors="replace").splitlines()
                theirs = expected.get(key, b"").decode(errors="replace").splitlines()
                print("    %s:" % key)
                for a, b in zip(mine, theirs):
                    if a != b:
                        print("      incremental %r\n      clean       %r" % (a, b))
                if len(mine) != len(theirs):
                    print("      %d vs %d lines" % (len(mine), len(theirs)))
        # Resynchronise so every later step is judged on its own edit.
        nift(root, "build", "--all")
    status = nift(root, "status").stdout
    if UP_TO_DATE.search(status):
        print("PASS [%s] %s: up to date afterwards" % (mode, step))
    else:
        failures.append((mode, step + " (perpetual rebuild)"))
        print("FAIL [%s] %s: not up to date after building:\n%s" % (mode, step, status))


PAGE = r"""<p>
O:@script { return open("data/open.txt") }
B:@script { return sha256(open_bytes("data/bytes.txt")) }
F:@script { fF := file("data/file.txt"); fF.open("r"); xF := fF.read_all(); fF.close(); return xF }
W:@script { fW := file("data/rw.txt"); fW.open("rw"); xW := fW.read_all(); fW.close(); return xW }
L:@script { sL := ifstream("data/line.txt"); xL := sL.read_line(); close(sL); return xL }
A:@script { sA := ifstream("data/all.txt"); xA := sA.read_all(); close(sA); return xA }
X:@script { sX := ifstream("data/tok.txt"); xX := ""; sX >> xX; close(sX); return xX }
E:@script { return open("EXT/abs.txt") }
P:@script { return open("../ext/parent.txt") }
S:@script { return open("data/link.txt") }
T:@script { sT := ifstream("data/link-stream.txt"); xT := sT.read_line(); close(sT); return xT }
M:@script { xM := ""; try { xM = open("data/missing-open.txt") } catch(e) { xM = "none" }; return xM }
N:@script { xN := ""; try { xN = sha256(open_bytes("data/missing-bytes.txt")) } catch(e) { xN = "none" }; return xN }
I:@script { xI := ""; try { sI := ifstream("data/missing-stream.txt"); xI = sI.read_line(); close(sI) } catch(e) { xI = "none" }; return xI }
J:@script { xJ := ""; try { fJ := file("data/missing-file.txt"); fJ.open("r"); xJ = fJ.read_all(); fJ.close() } catch(e) { xJ = "none" }; return xJ }
K:@script { xK := ""; try { xK = open("data/perm.txt") } catch(e) { xK = "denied" }; return xK }
Q:@script { xQ := ""; try { sQ := ifstream("data/perm-stream.txt"); xQ = sQ.read_line(); close(sQ) } catch(e) { xQ = "denied" }; return xQ }
G:@script { pG := page("other"); return pG.content }
H:@script { pH := page("other"); return pH.metadata.title }
U:@script { pU := page("fmpage"); return pU.metadata.title }
V:@script { gV := async (aV) => { return open(aV) }; return await gV("data/async.txt") }
Y:@script { gY := async (aY) => { return exists(aY) }; return await gY("data/async-missing.txt") }
@fn(rdthread(pT)) { sT2 := ifstream(pT); xT2 := sT2.read_line(); close(sT2); return xT2 }
R:@script { tR := thread(rdthread, "data/thread.txt"); return tR.join() }
D:@script { mkdir("copies"); cp("data/cp-src.txt", "copies/cp-copy.txt"); fD := file("data/fc-src.txt"); gD := fD.copy("copies/fc-copy.txt"); return "copied" }
</p>
"""


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def setup(work, mode):
    root, ext = work / "project", work / "ext"
    root.mkdir()
    ext.mkdir()
    nift(root, "init")
    config = root / ".nift" / "config.json"
    config.write_text(config.read_text().replace('"incremental-mode": "modified"', '"incremental-mode": "%s"' % mode))
    data = root / "data"
    for name in ("open", "bytes", "file", "rw", "line", "all", "tok"):
        write(data / (name + ".txt"), "%s-1\n" % name)
    write(ext / "abs.txt", "abs-1\n")
    write(ext / "parent.txt", "parent-1\n")
    write(ext / "target.txt", "target-1\n")
    write(ext / "stream-target.txt", "stream-target-1\n")
    (data / "link.txt").symlink_to(ext / "target.txt")
    (data / "link-stream.txt").symlink_to(ext / "stream-target.txt")
    write(data / "perm.txt", "perm-1\n")
    write(data / "perm-stream.txt", "perm-stream-1\n")
    if PERMISSIONS:
        os.chmod(data / "perm.txt", 0)
        os.chmod(data / "perm-stream.txt", 0)
    write(root / "content" / "index.html", PAGE.replace("EXT", str(ext)))
    nift(root, "track", "other", "Other")
    write(root / "content" / "other.html", "---\ntitle: meta-1\n---\nother-1\n")
    # A page whose metadata comes from an external front-matter file.
    nift(root, "track", "fmpage", "FmPage")
    write(root / "content" / "fmpage.html", "fm body\n")
    write(root / "meta" / "fmpage.json", '{"title": "ext-1"}\n')
    tracked = root / ".nift" / "tracked.json"
    document = json.loads(tracked.read_text())
    for entry in document["tracked"]:
        if entry["name"] == "fmpage":
            entry["frontmatter"] = "meta/fmpage.json"
    tracked.write_text(json.dumps(document, indent=2))
    write(data / "async.txt", "async-1\n")
    write(data / "thread.txt", "thread-1\n")
    write(data / "cp-src.txt", "cp-1\n")
    write(data / "fc-src.txt", "fc-1\n")

    return root, ext


def edit(path, text):
    # Keep the size and rewrite within the same second on purpose: the modes
    # must not depend on coarse mtimes or sizes to notice the edit.
    path.write_text(text)


def scenario(mode, work):
    root, ext = setup(work, mode)
    oracle = work / "oracle"
    data = root / "data"
    nift(root, "build", "--all")
    lines = page_lines(root)
    expect = ["O:open-1", "B:" + hashlib.sha256(b"bytes-1\n").hexdigest(), "F:file-1", "W:rw-1", "L:line-1", "A:all-1", "X:tok-1", "E:abs-1", "P:parent-1",
              "S:target-1", "T:stream-target-1", "M:none", "N:none", "I:none", "J:none", "H:meta-1",
              "U:ext-1", "V:async-1", "Y:false", "R:thread-1", "D:copied"]
    for prefix in expect:
        if prefix not in lines:
            failures.append((mode, "initial " + prefix))
            print("FAIL [%s] initial output lacks %s: %s" % (mode, prefix, lines))
    if PERMISSIONS and not any(line == "K:denied" for line in lines):
        failures.append((mode, "initial K:denied"))
        print("FAIL [%s] permission setup: %s" % (mode, lines))

    steps = [
        ("open() edit", lambda: edit(data / "open.txt", "open-2\n")),
        ("open_bytes() edit", lambda: edit(data / "bytes.txt", "bytes-2\n")),
        ("file().open(r) edit", lambda: edit(data / "file.txt", "file-2\n")),
        ("file().open(rw) edit", lambda: edit(data / "rw.txt", "rw-2\n")),
        ("ifstream read_line edit", lambda: edit(data / "line.txt", "line-2\n")),
        ("ifstream read_all edit", lambda: edit(data / "all.txt", "all-2\n")),
        ("ifstream extraction edit", lambda: edit(data / "tok.txt", "tok-2\n")),
        ("external absolute edit", lambda: edit(ext / "abs.txt", "abs-2\n")),
        ("external parent-traversal edit", lambda: edit(ext / "parent.txt", "parent-2\n")),
        ("symlink target edit (open)", lambda: edit(ext / "target.txt", "target-2\n")),
        ("symlink target edit (ifstream)", lambda: edit(ext / "stream-target.txt", "stream-target-2\n")),
        ("missing file appears (open)", lambda: write(data / "missing-open.txt", "appeared-open\n")),
        ("missing file appears (open_bytes)", lambda: write(data / "missing-bytes.txt", "appeared-bytes\n")),
        ("missing file appears (ifstream)", lambda: write(data / "missing-stream.txt", "appeared-stream\n")),
        ("missing file appears (file().open)", lambda: write(data / "missing-file.txt", "appeared-file\n")),
        ("appeared file edited (ifstream)", lambda: edit(data / "missing-stream.txt", "appeared-stream-2\n")),
        ("appeared file removed (open_bytes)", lambda: (data / "missing-bytes.txt").unlink()),
        ("page(x).content edit", lambda: edit(root / "content" / "other.html", "---\ntitle: meta-1\n---\nother-2\n")),
        ("page(x).metadata edit", lambda: edit(root / "content" / "other.html", "---\ntitle: meta-2\n---\nother-2\n")),
        ("page(x).metadata external front matter edit", lambda: edit(root / "meta" / "fmpage.json", '{"title": "ext-2"}\n')),
        ("async function open() edit", lambda: edit(data / "async.txt", "async-2\n")),
        ("async function exists() target appears", lambda: write(data / "async-missing.txt", "here\n")),
        ("thread() worker ifstream edit", lambda: edit(data / "thread.txt", "thread-2\n")),
        ("cp() source edit", lambda: edit(data / "cp-src.txt", "cp-2\n")),
        ("file().copy() source edit", lambda: edit(data / "fc-src.txt", "fc-2\n")),
    ]
    if PERMISSIONS:
        steps += [
            ("unreadable becomes readable (open)", lambda: os.chmod(data / "perm.txt", 0o644)),
            ("unreadable becomes readable (ifstream)", lambda: os.chmod(data / "perm-stream.txt", 0o644)),
            ("formerly unreadable edited", lambda: edit(data / "perm.txt", "perm-2\n")),
        ]
    else:
        print("SKIP [%s] permission steps (root without setpriv)" % mode)
    steps += [("no-op", lambda: None)]
    for name, action in steps:
        action()
        compare(mode, name, root, oracle)
    mid_stream(mode, root, oracle)
    move_source(mode, root)


def move_source(mode, root):
    """file().move()/mv(): the source is gone afterwards; it reappearing must
    rebuild the page (a clean build cannot be the oracle here: the first build
    consumed the source)."""
    write(root / "data" / "mv-a.txt", "mv-a-1\n")
    write(root / "data" / "mv-b.txt", "mv-b-1\n")
    nift(root, "track", "mover", "Mover")
    write(root / "content" / "mover.html",
          '<p>@script { xm := "a"; try { fm := file("data/mv-a.txt"); gm := fm.move("public/moved-a.html") } catch(e) { xm = "-" }; try { mv("data/mv-b.txt", "public/moved-b.html") } catch(e) { xm = xm + "-" }; return xm }</p>\n')
    nift(root, "build")
    status = nift(root, "status").stdout
    check_ok = UP_TO_DATE.search(status) is not None
    print(("PASS" if check_ok else "FAIL") + " [%s] move: up to date after moving the sources" % mode)
    if not check_ok:
        failures.append((mode, "move up to date"))
    for name, src in (("file().move", "mv-a"), ("mv()", "mv-b")):
        write(root / "data" / (src + ".txt"), src + "-2\n")
        status = nift(root, "status").stdout
        stale = UP_TO_DATE.search(status) is not None
        print(("FAIL" if stale else "PASS") + " [%s] %s: source reappearing rebuilds the page" % (mode, name))
        if stale:
            failures.append((mode, name + " source reappears"))
        nift(root, "build")
        moved = (root / "public" / ("moved-" + src[-1] + ".html")).read_text()
        ok = moved == src + "-2\n" and not (root / "data" / (src + ".txt")).exists()
        print(("PASS" if ok else "FAIL") + " [%s] %s: new source moved" % (mode, name))
        if not ok:
            failures.append((mode, name + " new source moved"))


MID_PAGE = r"""<p>
Z:@script { sz := ifstream("data/stream.txt"); a := sz.read_line(); if(exists("../ctl/sync")) { rm("../ctl/sync"); touch("../ctl/opened"); n := 0; while(!exists("../ctl/edited") && n < 3000) { sleep(10); n = n + 1 }; rm("../ctl/edited") }; b := sz.read_line(); close(sz); return a + "|" + b }
</p>
"""


def mid_stream(mode, root, oracle):
    """The file changes after the page opened it and before it finished
    reading. The next build must not report the page up to date."""
    ctl = root.parent / "ctl"
    ctl.mkdir(exist_ok=True)
    write(root / "data" / "stream.txt", "s1\ns2\n")
    # A second tracked page keeps this scenario independent of index.
    nift(root, "track", "streamed", "Streamed")
    write(root / "content" / "streamed.html", MID_PAGE)
    nift(root, "build")
    (ctl / "sync").write_text("")
    proc = subprocess.Popen(PRIV + [NIFT, "build"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                            preexec_fn=bounded)
    deadline = time.monotonic() + 30
    while not (ctl / "opened").exists() and time.monotonic() < deadline and proc.poll() is None:
        time.sleep(0.01)
    if not (ctl / "opened").exists():
        proc.kill()
        out, err = proc.communicate()
        failures.append((mode, "mid-stream setup"))
        print("FAIL [%s] mid-stream: page never reached the sync point\n%s%s" % (mode, out, err))
        return
    (root / "data" / "stream.txt").write_text("t1\nt2\n")
    (ctl / "edited").write_text("")
    out, err = proc.communicate(timeout=60)
    (ctl / "opened").unlink()
    if proc.returncode != 0:
        failures.append((mode, "mid-stream build"))
        print("FAIL [%s] mid-stream build failed:\n%s%s" % (mode, out, err))
        return
    status = nift(root, "status").stdout
    if UP_TO_DATE.search(status):
        failures.append((mode, "mid-stream edit"))
        print("FAIL [%s] edit during streaming: page reported up to date with stale output" % mode)
    else:
        print("PASS [%s] edit during streaming: page needs rebuilding" % mode)
    compare(mode, "after edit during streaming", root, oracle)


def main():
    if not os.path.exists(NIFT):
        sys.exit("nift binary not found: " + NIFT)
    for mode in MODES:
        work = Path(tempfile.mkdtemp(prefix="host-deps-%s-" % mode))
        try:
            scenario(mode, work)
        except Exception as error:  # report and continue with the next mode
            failures.append((mode, "exception"))
            print("FAIL [%s] %s" % (mode, error))
        finally:
            if KEEP:
                print("kept", work)
            else:
                for path in work.rglob("*"):
                    if not path.is_symlink():
                        try:
                            os.chmod(path, 0o755 if path.is_dir() else 0o644)
                        except OSError:
                            pass
                shutil.rmtree(work, ignore_errors=True)
    if failures:
        print("%d failure(s)" % len(failures))
        return 1
    print("all host-deps checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
