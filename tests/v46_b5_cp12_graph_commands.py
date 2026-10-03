#!/usr/bin/env python3
# CP12: end-to-end graph package commands. Real local + git (file://) fixtures,
# no network. Exercises transitive closure, conflicts/cycles, targeted/full
# update, orphan cleanup, v1->v2 migration, offline locked install, and runtime
# import from a v2 lock.
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


def make_local(root, name, value, deps=None, entry_import=None):
    pkg = root / name
    (pkg / "src").mkdir(parents=True)
    manifest = {"name": name, "version": "0.1.0", "entry": "src/main.f"}
    if deps:
        manifest["dependencies"] = deps
    (pkg / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    imports = f'import("{entry_import}")\n' if entry_import else ""
    (pkg / "src/main.f").write_text(f'{imports}v_{name} := "{value}"\nexport(v_{name})\n')
    return pkg


def lock_doc(site):
    return json.loads((site / ".nift/packages.lock.json").read_text(encoding="utf-8"))


def imported_value(site, name, var=None):
    if var is None:
        var = f"v_{name}"
    (site / "test.f").write_text(f'import("{name}")\nprint({var})\n')
    return run(site, "test.f").stdout.strip()


def v1_lock_text(site, name, source, ref, commit):
    (site / ".nift").mkdir(parents=True, exist_ok=True)
    lock = {name: {"source": source, "requested": ref, "commit": commit}}
    (site / ".nift/packages.lock.json").write_text(json.dumps(lock, indent=2) + "\n")


def main():
    tmp = Path(tempfile.mkdtemp(prefix="nift-cp12-"))
    try:
        # ---- 1. local transitive add/install/runtime ----
        site = tmp / "site1"; (site / ".nift").mkdir(parents=True)
        (site / "manifest.json").write_text('{"dependencies":{}}\n')
        make_local(tmp / "s1", "b", "bb")
        a = make_local(tmp / "s1", "a", "aa", deps={"b": {"source": "../b", "ref": "local"}},
                       entry_import="b")
        run(site, "add", str(a))
        if set(d.name for d in (site / ".nift/packages").iterdir()) != {"a", "b"}:
            fail("local transitive add did not install a + b")
        doc = lock_doc(site)
        if doc["lockfileVersion"] != 2 or "b" not in doc["packages"]:
            fail("local add did not emit a v2 graph lock with b")
        if doc["packages"]["a"]["requirements"]["b"]["source"] != "../b":
            fail("local edge requirement source not preserved")
        if doc["packages"]["b"]["source"] != str((tmp / "s1" / "b").resolve()):
            fail("local b canonical source wrong")
        if imported_value(site, "a") != "aa":
            fail("runtime import from local transitive closure failed")
        # orphan cleanup
        run(site, "remove", "a")
        if (site / ".nift/packages").exists() and any((site / ".nift/packages").iterdir()):
            fail("remove last root did not drop orphans")
        if lock_doc(site)["packages"] != {}:
            fail("remove last root did not empty the graph lock")

        # ---- 2. git transitive add + locked offline install + conflict diamond ----
        b_repo = make_repo(tmp / "s2", "b", "b1")
        b1 = subprocess.check_output(["git", "-C", str(b_repo), "rev-parse", "HEAD"], text=True).strip()
        a_repo = make_repo(tmp / "s2", "a", "a1", deps={"b": {"source": f"file://{b_repo}", "ref": b1}},
                           entry_import="b")
        site = tmp / "site2"; (site / ".nift").mkdir(parents=True)
        (site / "manifest.json").write_text('{"dependencies":{}}\n')
        run(site, "add", f"file://{a_repo}")
        doc = lock_doc(site)
        if "b" not in doc["packages"] or doc["packages"]["b"]["commit"] != b1:
            fail("git transitive add did not pin b at its commit")
        if imported_value(site, "a") != "a1":
            fail("git transitive runtime import failed")
        # locked offline install after wiping the store
        shutil.rmtree(site / ".nift/packages")
        run(site, "install")
        doc = lock_doc(site)
        if doc["packages"]["b"]["commit"] != b1:
            fail("locked install changed b's pinned commit")
        if imported_value(site, "a") != "a1":
            fail("locked install runtime import failed")
        # conflict diamond: add c requiring b at a NEW commit -> rollback
        (b_repo / "src/main.f").write_text('value := "b2"\nexport(value)\n')
        git(b_repo, "add", "."); git(b_repo, "commit", "-qm", "b2")
        b2 = subprocess.check_output(["git", "-C", str(b_repo), "rev-parse", "HEAD"], text=True).strip()
        c_repo = make_repo(tmp / "s2", "c", "c1", deps={"b": {"source": f"file://{b_repo}", "ref": b2}},
                           entry_import="b")
        before_lock = (site / ".nift/packages.lock.json").read_text(encoding="utf-8")
        res = run(site, "add", f"file://{c_repo}", ok=False)
        if res.returncode == 0:
            fail("conflict diamond add unexpectedly succeeded")
        if (site / ".nift/packages.lock.json").read_text(encoding="utf-8") != before_lock:
            fail("conflict diamond add mutated the lock")
        if any(p.name == "c" for p in (site / ".nift/packages").iterdir()):
            fail("conflict diamond add left a partial store change")

        # ---- 3. compatible diamond: both refs resolve to the same commit ----
        site = tmp / "site3"; (site / ".nift").mkdir(parents=True)
        (site / "manifest.json").write_text('{"dependencies":{}}\n')
        b1_repo = make_repo(tmp / "s3", "b", "b1")
        commit = subprocess.check_output(["git", "-C", str(b1_repo), "rev-parse", "HEAD"], text=True).strip()
        a1 = make_repo(tmp / "s3", "a", "a1", deps={"b": {"source": f"file://{b1_repo}", "ref": commit}}, entry_import="b")
        c1 = make_repo(tmp / "s3", "c", "c1", deps={"b": {"source": f"file://{b1_repo}", "ref": commit}}, entry_import="b")
        run(site, "add", f"file://{a1}")
        run(site, "add", f"file://{c1}")
        doc = lock_doc(site)
        if len(doc["packages"]) != 3:
            fail("compatible diamond did not produce 3 nodes")
        if doc["packages"]["a"]["requirements"]["b"]["requested"] != commit:
            fail("compatible diamond lost a's edge requirement")

        # ---- 4. targeted update conflict rollback (floating refs) ----
        site = tmp / "site4"; (site / ".nift").mkdir(parents=True)
        (site / "manifest.json").write_text('{"dependencies":{}}\n')
        btu = make_repo(tmp / "s4", "b", "1")
        atu = make_repo(tmp / "s4", "a", "1", deps={"b": {"source": f"file://{btu}", "ref": "latest"}}, entry_import="b")
        ctu = make_repo(tmp / "s4", "c", "1", deps={"b": {"source": f"file://{btu}", "ref": "latest"}}, entry_import="b")
        run(site, "add", f"file://{atu}")
        run(site, "add", f"file://{ctu}")
        (btu / "src/main.f").write_text('value := "2"\nexport(value)\n')
        git(btu, "add", "."); git(btu, "commit", "-qm", "b2")
        before = (site / ".nift/packages.lock.json").read_text(encoding="utf-8")
        res = run(site, "update", "atu", ok=False)
        if res.returncode == 0:
            fail("targeted update conflict unexpectedly succeeded")
        if (site / ".nift/packages.lock.json").read_text(encoding="utf-8") != before:
            fail("targeted update conflict mutated the lock")
        # full update reconciles both roots at the new commit
        run(site, "update")
        doc = lock_doc(site)
        bcommits = {doc["packages"]["a"]["requirements"]["b"]["requested"],
                    doc["packages"]["c"]["requirements"]["b"]["requested"]}
        if len(bcommits) != 1:
            fail("full update did not reconcile shared b")

        # ---- 5. v1 -> v2 migration on install ----
        site = tmp / "site5"; (site / ".nift").mkdir(parents=True)
        pkg = make_local(tmp / "s5", "mig", "m1")
        (site / "manifest.json").write_text(
            json.dumps({"dependencies": {"mig": {"source": str(pkg), "ref": "local"}}}, indent=2) + "\n")
        v1_lock_text(site, "mig", str(pkg), "local", "local")
        run(site, "install")
        doc = lock_doc(site)
        if doc["lockfileVersion"] != 2 or "mig" not in doc["packages"]:
            fail("v1 -> v2 migration did not emit a v2 graph lock")
        if imported_value(site, "mig") != "m1":
            fail("v1 -> v2 migration runtime import failed")

        # ---- 6. failed operation does not migrate a v1 lock ----
        site = tmp / "site6"; (site / ".nift").mkdir(parents=True)
        pkg = make_local(tmp / "s6", "mig2", "m2")
        (site / "manifest.json").write_text(
            json.dumps({"dependencies": {"mig2": {"source": str(pkg), "ref": "local"}}}, indent=2) + "\n")
        v1_lock_text(site, "mig2", str(pkg), "local", "local")
        res = run(site, "add", str(tmp / "missing-dir"), ok=False)  # source is not a directory
        if (site / ".nift/packages.lock.json").read_text(encoding="utf-8").find("lockfileVersion") != -1:
            fail("failed operation migrated a v1 lock")

        # ---- 7a. crash/recovery across a real multi-node v2 graph operation ----
        for seam in ("after-journal", "after-backup", "after-promote", "after-manifest", "after-lock", "during-cleanup"):
            site = tmp / f"sitecrash-{seam}"; (site / ".nift").mkdir(parents=True)
            (site / "manifest.json").write_text('{"dependencies":{}}\n')
            crm = make_repo(tmp / f"sc-{seam}", "a", "a1",
                            deps={"b": {"source": f"file://{b_repo}", "ref": b1}}, entry_import="b")
            run(site, "add", f"file://{crm}")
            # wipe the store, then crash during a fresh install of the 2-node graph
            shutil.rmtree(site / ".nift/packages")
            run(site, "install", env={"NIFT_TEST_PACKAGE_TXN_CRASH": seam}, ok=False)
            # recovery on a subsequent install must reach the full graph
            run(site, "install")
            names = sorted(p.name for p in (site / ".nift/packages").iterdir())
            if names != ["a", "b"]:
                fail(f"crash seam {seam}: recovery did not rebuild the full graph: {names}")
            if imported_value(site, "a") != "a1":
                fail(f"crash seam {seam}: recovered graph import failed")

        # ---- 7. read-only v1 is not rewritten (runtime import) ----
        site = tmp / "site7"; (site / ".nift").mkdir(parents=True)
        pkg = make_local(tmp / "s7", "mig3", "m3")
        (site / "manifest.json").write_text(
            json.dumps({"dependencies": {"mig3": {"source": str(pkg), "ref": "local"}}}, indent=2) + "\n")
        v1_lock_text(site, "mig3", str(pkg), "local", "local")
        (site / ".nift/packages").mkdir(parents=True, exist_ok=True)
        shutil.copytree(pkg, site / ".nift/packages/mig3", dirs_exist_ok=True)
        if imported_value(site, "mig3") != "m3":
            fail("runtime import from a v1 lock failed")
        if (site / ".nift/packages.lock.json").read_text(encoding="utf-8").find("lockfileVersion") != -1:
            fail("read-only import rewrote a v1 lock")

    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print(f"v46_b5_cp12_graph_commands: {failures} failure(s)")
        return 1
    print("v46_b5_cp12_graph_commands: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())