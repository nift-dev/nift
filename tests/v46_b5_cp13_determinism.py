#!/usr/bin/env python3
# CP13: determinism, reproducibility, no-op stability, and failure immutability
# of the v2 graph model. Real git (file://) fixtures, no network.
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

NIFT = str(Path(sys.argv[1] if len(sys.argv) > 1 else "./nift").resolve())
failures = 0


def fail(msg):
    global failures
    print(f"FAIL: {msg}")
    failures += 1


def run(cwd, *args, env=None, ok=True):
    merged = os.environ.copy()
    if env:
        merged.update(env)
    result = subprocess.run([NIFT, *args], cwd=cwd, env=merged,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=60)
    if ok and result.returncode != 0:
        raise AssertionError(f"{' '.join(args)} failed: {result.stderr}")
    return result


def git(cwd, *args):
    subprocess.run(["git", "-C", str(cwd), *args], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def make_repo(root, name, value, deps=None, entry_import=None):
    repo = root / name
    (repo / "src").mkdir(parents=True)
    manifest = {"name": name, "version": "0.1.0", "entry": "src/main.f"}
    if deps:
        manifest["dependencies"] = deps
    (repo / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    imports = f'import("{entry_import}")\n' if entry_import else ""
    (repo / "src/main.f").write_text(f'{imports}v_{name} := "{value}"\nexport(v_{name})\n')
    git(repo, "init", "-q")
    git(repo, "config", "user.email", "a@b.c")
    git(repo, "config", "user.name", "test")
    git(repo, "add", ".")
    git(repo, "commit", "-qm", "one")
    return repo


def lock_text(site):
    return (site / ".nift/packages.lock.json").read_text(encoding="utf-8")


def query(site, name=""):
    return run(site, "packages", *([name] if name else [])).stdout


def build_graph_site(site, b_repo, a_repo, root_deps):
    (site / ".nift").mkdir(parents=True)
    (site / "manifest.json").write_text(json.dumps({"dependencies": root_deps}, indent=2) + "\n")
    for name, src in root_deps.items():
        run(site, "add", f"file://{src}" if not name == "x" else f"file://{src}")


def main():
    tmp = Path(tempfile.mkdtemp(prefix="nift-cp13-"))
    try:
        # ---- reproducibility: two independent projects, byte-identical ----
        b = make_repo(tmp / "r1", "b", "b1")
        bcommit = subprocess.check_output(["git", "-C", str(b), "rev-parse", "HEAD"], text=True).strip()
        a1 = make_repo(tmp / "r2", "a", "a1", deps={"b": {"source": f"file://{b}", "ref": bcommit}}, entry_import="b")
        c1 = make_repo(tmp / "r3", "c", "c1", deps={"b": {"source": f"file://{b}", "ref": bcommit}}, entry_import="b")

        site1 = tmp / "s1"; (site1 / ".nift").mkdir(parents=True)
        (site1 / "manifest.json").write_text('{"dependencies":{}}\n')
        run(site1, "add", f"file://{a1}")
        run(site1, "add", f"file://{c1}")

        site2 = tmp / "s2"; (site2 / ".nift").mkdir(parents=True)
        # same root set, reversed insertion order and different checkout path depth
        (site2 / "manifest.json").write_text('{"dependencies":{}}\n')
        run(site2, "add", f"file://{c1}")
        run(site2, "add", f"file://{a1}")

        if lock_text(site1) != lock_text(site2):
            fail("reproducibility: byte-identical locks differ")
        if query(site1) != query(site2):
            fail("reproducibility: byte-identical query output differs")
        qj1 = run(site1, "packages", "--json").stdout
        qj2 = run(site2, "packages", "--json").stdout
        if qj1 != qj2:
            fail("reproducibility: byte-identical JSON output differs")

        # exact Git node identity in both
        if lock_text(site1).find(bcommit) == -1:
            fail("reproducibility: exact Git commit not pinned")

        # query works with .nift/packages absent
        shutil.rmtree(site1 / ".nift/packages")
        q = query(site1, "b")
        if "a -> b" not in q or "c -> b" not in q:
            fail("query does not enumerate both diamond paths from manifest+lock")

        # ---- CWD independence: run the query from a different CWD ----
        cwd_before = os.getcwd()
        os.chdir(tmp)
        q_other = run(site1, "packages", "--json").stdout
        os.chdir(cwd_before)
        if q_other != qj1:
            fail("query output depends on the process CWD")

        # ---- no-op stability: install with complete graph + valid store ----
        run(site1, "install")  # restore the store
        before = lock_text(site1)
        run(site1, "install")  # no-op
        if lock_text(site1) != before:
            fail("no-op install rewrote the lock bytes")

        # ---- failure immutability: conflict add leaves everything unchanged ----
        # advance b so a new root requiring b@latest conflicts with the pinned c
        (b / "src/main.f").write_text('v_b := "b2"\nexport(v_b)\n')
        git(b, "add", "."); git(b, "commit", "-qm", "b2")
        d = make_repo(tmp / "r4", "d", "d1", deps={"b": {"source": f"file://{b}", "ref": "latest"}}, entry_import="b")
        site3 = tmp / "s3"; (site3 / ".nift").mkdir(parents=True)
        (site3 / "manifest.json").write_text('{"dependencies":{}}\n')
        run(site3, "add", f"file://{a1}")  # a -> b@B1
        m_before = (site3 / "manifest.json").read_text(encoding="utf-8")
        l_before = lock_text(site3)
        res = run(site3, "add", f"file://{d}", ok=False)  # d -> b@B2 -> conflict
        if res.returncode == 0:
            fail("failure-immutability: conflict add unexpectedly succeeded")
        if (site3 / "manifest.json").read_text(encoding="utf-8") != m_before:
            fail("failure-immutability: conflict add mutated the manifest")
        if lock_text(site3) != l_before:
            fail("failure-immutability: conflict add mutated the lock")

        # ---- locked install does not re-resolve floating refs ----
        # (the lock pins b at B1; install must not contact the repo to resolve
        #  'latest' - it clones only at the pinned commit)
        shutil.rmtree(site3 / ".nift/packages")
        run(site3, "install")
        doc = json.loads(lock_text(site3))
        installed_b = doc["packages"]["b"]["commit"]
        if installed_b == subprocess.check_output(["git", "-C", str(b), "rev-parse", "HEAD"], text=True).strip():
            fail("locked install re-resolved a floating ref to the new commit")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print(f"v46_b5_cp13_determinism: {failures} failure(s)")
        return 1
    print("v46_b5_cp13_determinism: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())