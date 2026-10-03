#!/usr/bin/env python3
# CP10a: PackageTransaction journal recovery understands v2 graph lock payloads
# without changing the journal version. Constructs interrupted-transaction
# journals directly and triggers recovery via a failing package command.
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

NIFT = str(Path(sys.argv[1] if len(sys.argv) > 1 else "./nift").resolve())


def run(cwd, *args, env=None, ok=True):
    merged = os.environ.copy()
    if env:
        merged.update(env)
    result = subprocess.run([NIFT, *args], cwd=cwd, env=merged,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                            timeout=30)
    if ok and result.returncode != 0:
        raise AssertionError(f"{' '.join(args)} failed: {result.stderr}")
    return result


def make_local_pkg(root, name, value):
    pkg = root / name
    (pkg / "src").mkdir(parents=True)
    (pkg / "manifest.json").write_text(
        f'{{"name":"{name}","version":"0.1.0","entry":"src/main.f"}}\n', encoding="utf-8")
    (pkg / "src/main.f").write_text(f'value := "{value}"\nexport(value)\n', encoding="utf-8")
    return pkg


def dump(doc):
    return json.dumps(doc, indent=2) + "\n"


def setup(tmp, value):
    """A site with demo installed (v1 lock) exporting `value`."""
    site = tmp / "site"
    (site / ".nift").mkdir(parents=True)
    pkg = make_local_pkg(tmp, "demo", value)
    (site / "manifest.json").write_text('{"dependencies":{}}\n')
    run(site, "add", str(pkg))
    return site, pkg


def build_v2_journal(site, pkg, lock, commit="local"):
    """Construct a .txn staging + a v2 graph-lock journal for a demo replace."""
    transaction = ".txn-99999-1-0"
    txn_root = site / ".nift/packages" / transaction
    (txn_root / "new").mkdir(parents=True)
    (txn_root / "old").mkdir()
    make_local_pkg(txn_root / "new", "demo", "new")
    desired_manifest = {"dependencies": {"demo": {"source": str(pkg), "ref": "local"}}}
    journal = {
        "version": 1,
        "transaction": transaction,
        "operations": [{"name": "demo", "kind": "replace", "hadOld": True, "commit": commit}],
        "manifest": desired_manifest,
        "lock": lock,
        "oldManifest": (site / "manifest.json").read_text(encoding="utf-8"),
        "oldLock": (site / ".nift/packages.lock.json").read_text(encoding="utf-8"),
    }
    (site / ".nift/package-transaction.json").write_text(dump(journal), encoding="utf-8")


def demo_value(site):
    return (site / ".nift/packages/demo/src/main.f").read_text(encoding="utf-8")


def lock_doc(site):
    return json.loads((site / ".nift/packages.lock.json").read_text(encoding="utf-8"))


def main():
    tmp = Path(tempfile.mkdtemp(prefix="nift-cp10a-"))
    failures = 0
    try:
        # ---- valid v2 journal payload: recovery accepts and replays ----
        site, pkg = setup(tmp, "old")
        lock = {"lockfileVersion": 2, "packages": {
            "demo": {"source": str(pkg.resolve()), "commit": "local", "requirements": {}}}}
        build_v2_journal(site, pkg, lock)
        bad = tmp / "bad"; bad.mkdir()
        run(site, "add", str(bad), ok=False)  # recovery runs, then add fails
        if "new" not in demo_value(site):
            print("FAIL: v2 recovery did not promote the staged package"); failures += 1
        if lock_doc(site).get("lockfileVersion") != 2:
            print("FAIL: v2 recovery did not publish the v2 lock"); failures += 1
        if (site / ".nift/package-transaction.json").exists():
            print("FAIL: v2 recovery did not clean the journal"); failures += 1
        if any(p.name.startswith(".txn-") for p in (site / ".nift/packages").iterdir()):
            print("FAIL: v2 recovery left staging behind"); failures += 1

        # ---- malformed v2 journal payloads: recovery rejects without mutation ----
        cases = {
            "unsupported-version": {"lockfileVersion": 3, "packages": {}},
            "missing-edge-target": {"lockfileVersion": 2, "packages": {
                "demo": {"source": "/abs/demo", "commit": "local",
                         "requirements": {"missing": {"source": "s", "requested": "latest"}}}}},
            "orphan": {"lockfileVersion": 2, "packages": {
                "demo": {"source": "/abs/demo", "commit": "local", "requirements": {}},
                "orphan": {"source": "/abs/orphan", "commit": "local", "requirements": {}}}},
            "cycle": {"lockfileVersion": 2, "packages": {
                "demo": {"source": "/abs/demo", "commit": "local",
                         "requirements": {"demo": {"source": "/abs/demo", "requested": "latest"}}}}},
            "malformed-node": {"lockfileVersion": 2, "packages": {
                "demo": {"source": "/abs/demo", "commit": "not-a-commit", "requirements": {}}}},
            "malformed-edge": {"lockfileVersion": 2, "packages": {
                "demo": {"source": "/abs/demo", "commit": "local",
                         "requirements": {"z": {"source": "s"}}}}},
        }
        for label, bad_lock in cases.items():
            case = tmp / label
            site2, pkg2 = setup(case, "old")
            before_lock = (site2 / ".nift/packages.lock.json").read_text(encoding="utf-8")
            build_v2_journal(site2, pkg2, bad_lock)
            (case / "bad").mkdir()  # a valid dir lacking a manifest: add reaches acquire/recovery
            res = run(site2, "add", str(case / "bad"), ok=False)
            if res.returncode == 0:
                print(f"FAIL: {label} recovery unexpectedly succeeded"); failures += 1
            if "new" in demo_value(site2):
                print(f"FAIL: {label} recovery mutated the store"); failures += 1
            if not (site2 / ".nift/package-transaction.json").exists():
                print(f"FAIL: {label} recovery cleaned an unapplied journal"); failures += 1
            if (site2 / ".nift/packages.lock.json").read_text(encoding="utf-8") != before_lock:
                print(f"FAIL: {label} recovery modified the lock"); failures += 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print(f"v46_b5_cp10a_recovery_smoke: {failures} failure(s)")
        return 1
    print("v46_b5_cp10a_recovery_smoke: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())