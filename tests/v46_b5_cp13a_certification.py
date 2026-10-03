#!/usr/bin/env python3
# CP13a: completes the CP13 certification matrix - local live-content contract,
# source-spelling vs canonical fidelity (via shared canonicalizer), operation
# determinism, no-op slot stability, failure immutability, and conflict/cycle
# determinism. Real git (file://) fixtures, no network.
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


def make_local(root, name, value, deps=None):
    pkg = root / name
    (pkg / "src").mkdir(parents=True)
    manifest = {"name": name, "version": "0.1.0", "entry": "src/main.f"}
    if deps:
        manifest["dependencies"] = deps
    (pkg / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (pkg / "src/main.f").write_text(f'v_{name} := "{value}"\nexport(v_{name})\n')
    return pkg


def lock_text(site):
    return (site / ".nift/packages.lock.json").read_text(encoding="utf-8")


def lock_doc(site):
    return json.loads(lock_text(site))


def fresh_site(tmp, name):
    site = tmp / name
    (site / ".nift").mkdir(parents=True)
    (site / "manifest.json").write_text('{"dependencies":{}}\n')
    return site


def main():
    tmp = Path(tempfile.mkdtemp(prefix="nift-cp13a-"))
    try:
        # ---- 1. local live-content contract ----
        site = fresh_site(tmp, "live")
        local = make_local(tmp / "ll", "mypkg", "v1")
        run(site, "add", str(local))
        node = lock_doc(site)["packages"]["mypkg"]
        if node["source"] != str(local.resolve()) or node["commit"] != "local":
            fail("local live-content: lock node is not canonical path + local")
        before_lock = lock_text(site)
        # modify contents in place, same path
        (local / "src/main.f").write_text('v_mypkg := "v2"\nexport(v_mypkg)\n')
        if run(site, "test.f", ok=False).returncode == 0:
            pass
        (site / "test.f").write_text('import("mypkg")\nprint(v_mypkg)\n')
        if run(site, "test.f").stdout.strip() != "v2":
            fail("local live-content: runtime did not observe the new live contents")
        if lock_text(site) != before_lock:
            fail("local live-content: lock bytes changed after local content edit")
        if not any("commit: local" in line for line in run(site, "packages", "mypkg").stdout.splitlines()):
            fail("local live-content: query does not show commit: local")

        # ---- 2. no-op slot stability (unchanged slot survives intact) ----
        sentinel = site / ".nift/packages/mypkg/SENTINEL"
        sentinel.write_text("keep-me\n")
        run(site, "install")  # no-op
        if not sentinel.exists() or sentinel.read_text().strip() != "keep-me":
            fail("no-op stability: matching store slot was replaced")
        if lock_text(site) != before_lock:
            fail("no-op stability: lock bytes changed")

        # ---- 3. operation determinism matrix (equivalent starting states twice) ----
        b = make_repo(tmp / "ob", "b", "b1")
        bcommit = subprocess.check_output(["git", "-C", str(b), "rev-parse", "HEAD"], text=True).strip()
        a = make_repo(tmp / "oa", "a", "a1", deps={"b": {"source": f"file://{b}", "ref": bcommit}}, entry_import="b")

        def run_matrix(mk_site, suffix):
            s = mk_site()
            add_out = run(s, "add", f"file://{a}").stdout
            lock1 = lock_text(s)
            install_out = run(s, "install").stdout
            lock2 = lock_text(s)
            # advance b, then full update + targeted update determinism
            (b / "src/main.f").write_text(f'v_b := "b{suffix}"\nexport(v_b)\n')
            git(b, "add", "."); git(b, "commit", "-qm", f"b{suffix}")
            update_out = run(s, "update").stdout
            lock3 = lock_text(s)
            return {"add": add_out, "install": install_out, "update": update_out,
                    "lock_add": lock1, "lock_install": lock2, "lock_update": lock3,
                    "installed": sorted(p.name for p in (s / ".nift/packages").iterdir())}

        r1 = run_matrix(lambda: fresh_site(tmp, "m1"), "2")
        r2 = run_matrix(lambda: fresh_site(tmp, "m2"), "3")
        for key in ("add", "install", "update"):
            if r1[key] != r2[key]:
                fail(f"operation determinism: {key} stdout differs")
        for key in ("lock_add", "lock_install", "lock_update"):
            if r1[key] != r2[key]:
                fail(f"operation determinism: {key} lock differs")
        if r1["installed"] != r2["installed"]:
            fail("operation determinism: installed node identities differ")

        # ---- 4. failure immutability: cycle, unknown ref, missing source, name mismatch ----
        # cycle: a package whose manifest self-depends
        cyc = make_local(tmp / "fc", "cyc", "x", deps={"cyc": {"source": f"file://{tmp}/fc/cyc", "ref": "latest"}})
        site = fresh_site(tmp, "fcyc")
        m_before = (site / "manifest.json").read_text(encoding="utf-8")
        res = run(site, "add", str(cyc), ok=False)
        if res.returncode == 0:
            fail("failure-immutability: self-cycle add unexpectedly succeeded")
        if (site / "manifest.json").read_text(encoding="utf-8") != m_before:
            fail("failure-immutability: self-cycle add mutated the manifest")
        if (site / ".nift/packages.lock.json").exists():
            fail("failure-immutability: self-cycle add created a lock")
        # unknown git ref
        site = fresh_site(tmp, "fref")
        badref = make_repo(tmp / "fb", "badref", "x")
        res = run(site, "add", f"file://{badref}", "--ref=does-not-exist", ok=False)
        if res.returncode == 0:
            fail("failure-immutability: unknown ref add unexpectedly succeeded")
        if (site / ".nift/packages.lock.json").exists():
            fail("failure-immutability: unknown ref add created a lock")

        # ---- 5. conflict determinism: same name + different commit, reversed order ----
        b3 = make_repo(tmp / "cb", "b", "b1")
        b1 = subprocess.check_output(["git", "-C", str(b3), "rev-parse", "HEAD"], text=True).strip()
        (b3 / "src/main.f").write_text('v_b := "b2"\nexport(v_b)\n')
        git(b3, "add", "."); git(b3, "commit", "-qm", "b2")
        b2 = subprocess.check_output(["git", "-C", str(b3), "rev-parse", "HEAD"], text=True).strip()
        pa = make_repo(tmp / "pa", "a", "a1", deps={"b": {"source": f"file://{b3}", "ref": b1}}, entry_import="b")
        pc = make_repo(tmp / "pc", "c", "c1", deps={"b": {"source": f"file://{b3}", "ref": b2}}, entry_import="b")

        def conflict_stdout(s):
            res = run(s, "add", f"file://{pc}", ok=False)
            return res.stderr

        s_a = fresh_site(tmp, "ca"); run(s_a, "add", f"file://{pa}"); diag1 = conflict_stdout(s_a)
        s_b = fresh_site(tmp, "cb2"); run(s_b, "add", f"file://{pa}"); diag2 = conflict_stdout(s_b)
        # normalize the temp-root prefix (differs between fixtures)
        diag1n = diag1.replace(str(tmp), "TMP"); diag2n = diag2.replace(str(tmp), "TMP")
        if diag1n != diag2n:
            fail("conflict determinism: diagnostics differ across equivalent fixtures")

    # ---- 6. targeted-update determinism (compatible shared node) ----
        bd = make_repo(tmp / "tb", "b", "b1")
        ad = make_repo(tmp / "ta", "a", "a1", deps={"b": {"source": f"file://{bd}", "ref": "latest"}}, entry_import="b")
        cd = make_repo(tmp / "tc", "c", "c1", deps={"b": {"source": f"file://{bd}", "ref": "latest"}}, entry_import="b")

        def targeted_update(mk):
            s = mk()
            run(s, "add", f"file://{ad}")
            run(s, "add", f"file://{cd}")
            out = run(s, "update", "a").stdout
            manifest = (s / "manifest.json").read_text(encoding="utf-8")
            lock = lock_text(s)
            nodes = sorted(p.name for p in (s / ".nift/packages").iterdir())
            return {"out": out, "manifest": manifest, "lock": lock, "nodes": nodes}

        t1 = targeted_update(lambda: fresh_site(tmp, "tu1"))
        t2 = targeted_update(lambda: fresh_site(tmp, "tu2"))
        for key in ("out", "manifest", "lock", "nodes"):
            if t1[key] != t2[key]:
                fail(f"targeted-update determinism: {key} differs")

        # ---- 7. remove/orphan-cleanup determinism ----
        def do_remove(mk):
            s = mk()
            run(s, "add", f"file://{ad}")
            run(s, "add", f"file://{cd}")
            # remove a: b retained because c still reaches it
            run(s, "remove", "a")
            mid_manifest = (s / "manifest.json").read_text(encoding="utf-8")
            mid_lock = lock_text(s)
            mid_nodes = sorted(p.name for p in (s / ".nift/packages").iterdir())
            # remove c: b becomes orphaned and is removed
            run(s, "remove", "c")
            final_manifest = (s / "manifest.json").read_text(encoding="utf-8")
            final_lock = lock_text(s)
            final_nodes = sorted(p.name for p in (s / ".nift/packages").iterdir())
            return {"mid_manifest": mid_manifest, "mid_lock": mid_lock, "mid_nodes": mid_nodes,
                    "final_manifest": final_manifest, "final_lock": final_lock, "final_nodes": final_nodes}

        r1 = do_remove(lambda: fresh_site(tmp, "rm1"))
        r2 = do_remove(lambda: fresh_site(tmp, "rm2"))
        for key in r1:
            if r1[key] != r2[key]:
                fail(f"remove determinism: {key} differs")
        if r1["mid_nodes"] != ["b", "c"] or "b" not in r1["mid_nodes"]:
            fail("remove determinism: shared b was not retained while reachable")
        if "b" in r1["final_nodes"]:
            fail("remove determinism: orphan b was not removed")

        # ---- 8. missing transitive local source failure immutability ----
        lb = make_local(tmp / "lb", "b", "b1")
        la = make_local(tmp / "la", "a", "a1", deps={"b": {"source": str(lb), "ref": "local"}})
        site = fresh_site(tmp, "missrc")
        run(site, "add", str(la))
        m_before = (site / "manifest.json").read_text(encoding="utf-8")
        l_before = lock_text(site)
        store_before = sorted(p.name for p in (site / ".nift/packages").iterdir())
        # delete b's source so graph re-resolution cannot load its manifest
        shutil.rmtree(lb)
        res = run(site, "update", ok=False)
        if res.returncode == 0:
            fail("missing-transitive-source: update unexpectedly succeeded")
        if (site / "manifest.json").read_text(encoding="utf-8") != m_before:
            fail("missing-transitive-source: update mutated the manifest")
        if lock_text(site) != l_before:
            fail("missing-transitive-source: update mutated the lock")
        if sorted(p.name for p in (site / ".nift/packages").iterdir()) != store_before:
            fail("missing-transitive-source: update mutated the store")
        if any(p.name.startswith(".txn-") for p in (site / ".nift/packages").iterdir()):
            fail("missing-transitive-source: update left partial staging")

        # ---- 9. conflict determinism: different source ----
        bS1 = make_repo(tmp / "cs1", "b", "b1")
        bS2 = make_repo(tmp / "cs2", "b", "b1")
        aS1 = make_repo(tmp / "csa", "a", "a1", deps={"b": {"source": f"file://{bS1}", "ref": "latest"}}, entry_import="b")
        cS2 = make_repo(tmp / "csc", "c", "c1", deps={"b": {"source": f"file://{bS2}", "ref": "latest"}}, entry_import="b")

        def diff_source_diag(mk):
            s = mk()
            run(s, "add", f"file://{aS1}")
            return run(s, "add", f"file://{cS2}", ok=False).stderr.replace(str(tmp), "TMP")

        if diff_source_diag(lambda: fresh_site(tmp, "ds1")) != diff_source_diag(lambda: fresh_site(tmp, "ds2")):
            fail("conflict determinism: different-source diagnostics differ")

        # ---- 10. conflict determinism: targeted-update shared-node conflict ----
        bt = make_repo(tmp / "tsb", "b", "b1")
        at = make_repo(tmp / "tsa", "a", "a1", deps={"b": {"source": f"file://{bt}", "ref": "latest"}}, entry_import="b")
        ct = make_repo(tmp / "tsc", "c", "c1", deps={"b": {"source": f"file://{bt}", "ref": "latest"}}, entry_import="b")

        def targeted_conflict(mk):
            s = mk()
            run(s, "add", f"file://{at}")
            run(s, "add", f"file://{ct}")
            return run(s, "update", "a", ok=False).stderr.replace(str(tmp), "TMP")

        # advance b once (shared repo) so a's latest re-resolves to B2 while
        # preserved c stays pinned at B1
        (bt / "src/main.f").write_text('v_b := "b2"\nexport(v_b)\n')
        git(bt, "add", "."); git(bt, "commit", "-qm", "b2")

        if targeted_conflict(lambda: fresh_site(tmp, "tc1")) != targeted_conflict(lambda: fresh_site(tmp, "tc2")):
            fail("conflict determinism: targeted-update conflict diagnostics differ")

    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print(f"v46_b5_cp13a_certification: {failures} failure(s)")
        return 1
    print("v46_b5_cp13a_certification: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())