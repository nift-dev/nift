#!/usr/bin/env python3
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


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


def git(cwd, *args):
    subprocess.run(["git", "-C", str(cwd), *args], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def make_repo(root):
    repo = root / "repo"
    (repo / "src").mkdir(parents=True)
    (repo / "manifest.json").write_text(
        '{"name":"demo","version":"0.1.0","entry":"src/main.f"}\n', encoding="utf-8")
    (repo / "src/main.f").write_text('value := "one"\nexport(value)\n', encoding="utf-8")
    git(repo, "init", "-q")
    git(repo, "config", "user.email", "a@b.c")
    git(repo, "config", "user.name", "test")
    git(repo, "add", ".")
    git(repo, "commit", "-qm", "one")
    return repo


def advance_repo(repo):
    advance_repo.counter += 1
    value = f"two-{advance_repo.counter}"
    (repo / "src/main.f").write_text(f'value := "{value}"\nexport(value)\n', encoding="utf-8")
    git(repo, "add", ".")
    git(repo, "commit", "-qm", "two")
    commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    return commit, value


advance_repo.counter = 0


def assert_clean(site):
    assert not (site / ".nift/package-transaction.json").exists()
    package_root = site / ".nift/packages"
    if package_root.exists():
        assert not any(path.name.startswith(".txn-") for path in package_root.iterdir())


def installed_value(site):
    return (site / ".nift/packages/demo/src/main.f").read_text(encoding="utf-8")


if shutil.which("git") is None:
    print("SKIP package transactions (git unavailable)")
    raise SystemExit(77)

with tempfile.TemporaryDirectory() as raw:
    root = Path(raw).resolve()
    repo = make_repo(root)
    source = f"file://{repo}"

    # A pre-journal crash leaves only disposable private staging.
    site = root / "pre-journal"
    site.mkdir()
    crashed = run(site, "add", source, env={"NIFT_TEST_PACKAGE_TXN_CRASH": "after-stage"}, ok=False)
    assert crashed.returncode == 86
    assert not (site / "manifest.json").exists()
    run(site, "add", source)
    assert "one" in installed_value(site)
    assert_clean(site)

    # Every post-journal phase rolls forward before the next package command.
    for seam in ("after-journal", "after-backup", "after-promote", "after-manifest", "after-lock", "during-cleanup"):
        case = root / f"update-{seam}"
        case.mkdir()
        run(case, "add", source)
        latest, expected_value = advance_repo(repo)
        crashed = run(case, "update", "demo", env={"NIFT_TEST_PACKAGE_TXN_CRASH": seam}, ok=False)
        assert crashed.returncode == 86, (seam, crashed.stderr)
        assert (case / ".nift/package-transaction.json").exists() == (seam != "during-cleanup")
        run(case, "install")
        assert expected_value in installed_value(case), seam
        lock = json.loads((case / ".nift/packages.lock.json").read_text(encoding="utf-8"))
        assert lock["lockfileVersion"] == 2 and lock["packages"]["demo"]["commit"] == latest
        assert_clean(case)

    # Removal also rolls forward, even though the recovered manifest has no
    # dependencies and the triggering install subsequently reports that fact.
    remove_site = root / "remove"
    remove_site.mkdir()
    run(remove_site, "add", source)
    crashed = run(remove_site, "remove", "demo", env={"NIFT_TEST_PACKAGE_TXN_CRASH": "after-backup"}, ok=False)
    assert crashed.returncode == 86
    run(remove_site, "install", ok=False)
    assert not (remove_site / ".nift/packages/demo").exists()
    assert json.loads((remove_site / "manifest.json").read_text(encoding="utf-8"))["dependencies"] == {}
    assert_clean(remove_site)

    # Recovery refuses to overwrite unrelated metadata edits.
    changed = root / "manual-change"
    changed.mkdir()
    crashed = run(changed, "add", source, env={"NIFT_TEST_PACKAGE_TXN_CRASH": "after-journal"}, ok=False)
    assert crashed.returncode == 86
    (changed / "manifest.json").write_text('{"dependencies":{}}\n', encoding="utf-8")
    refused = run(changed, "install", ok=False)
    assert refused.returncode != 0
    assert "changed outside recovery" in refused.stderr
    assert (changed / ".nift/package-transaction.json").exists()

    # Metadata edits made while slow acquisition is staging are detected before
    # intent publication and never overwritten.
    staging_edit = root / "staging-edit"
    staging_edit.mkdir()
    run(staging_edit, "add", source)
    advance_repo(repo)
    stage_hold = root / "stage-hold"
    stage_hold.mkdir()
    env = os.environ.copy()
    env["NIFT_TEST_PACKAGE_TXN_STAGE_HOLD"] = str(stage_hold)
    updater = subprocess.Popen([NIFT, "update", "demo"], cwd=staging_edit, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.time() + 10
    while not (stage_hold / "staged").exists() and time.time() < deadline:
        time.sleep(0.01)
    assert (stage_hold / "staged").exists()
    original = (staging_edit / "manifest.json").read_text(encoding="utf-8")
    edited = original.replace('{\n  "dependencies"', '{\n  "name": "edited",\n  "version": "0.1.0",\n  "entry": "main.f",\n  "dependencies"')
    (staging_edit / "manifest.json").write_text(edited, encoding="utf-8")
    (stage_hold / "release").write_text("go\n", encoding="utf-8")
    _, update_err = updater.communicate(timeout=20)
    assert updater.returncode != 0
    assert "changed while packages were staged" in update_err
    assert (staging_edit / "manifest.json").read_text(encoding="utf-8") == edited
    assert not (staging_edit / ".nift/package-transaction.json").exists()

    # A malformed recovery journal fails closed without touching the live slot.
    malformed = root / "malformed-journal"
    malformed.mkdir()
    run(malformed, "add", source)
    before = installed_value(malformed)
    (malformed / ".nift/package-transaction.json").write_text('{"version":1}\n', encoding="utf-8")
    rejected = run(malformed, "install", ok=False)
    assert rejected.returncode != 0
    assert installed_value(malformed) == before
    assert (malformed / ".nift/package-transaction.json").exists()

    # Structurally valid journals must still contain complete package operations.
    incomplete = root / "incomplete-journal"
    incomplete.mkdir()
    run(incomplete, "add", source)
    old_value = installed_value(incomplete)
    advance_repo(repo)
    crashed = run(incomplete, "update", "demo", env={"NIFT_TEST_PACKAGE_TXN_CRASH": "after-journal"}, ok=False)
    assert crashed.returncode == 86
    journal_path = incomplete / ".nift/package-transaction.json"
    journal = json.loads(journal_path.read_text(encoding="utf-8"))
    journal["operations"] = []
    journal_path.write_text(json.dumps(journal, indent=2) + "\n", encoding="utf-8")
    rejected = run(incomplete, "install", ok=False)
    assert rejected.returncode != 0
    assert installed_value(incomplete) == old_value

    # Recovery revalidates staged content before displacing the last-good slot.
    corrupt_stage = root / "corrupt-stage"
    corrupt_stage.mkdir()
    run(corrupt_stage, "add", source)
    old_value = installed_value(corrupt_stage)
    advance_repo(repo)
    crashed = run(corrupt_stage, "update", "demo", env={"NIFT_TEST_PACKAGE_TXN_CRASH": "after-journal"}, ok=False)
    assert crashed.returncode == 86
    transaction_dirs = [path for path in (corrupt_stage / ".nift/packages").iterdir() if path.name.startswith(".txn-")]
    assert len(transaction_dirs) == 1
    (transaction_dirs[0] / "new/demo/manifest.json").write_text(
        '{"name":"wrong","version":"0.1.0","entry":"src/main.f"}\n', encoding="utf-8")
    rejected = run(corrupt_stage, "install", ok=False)
    assert rejected.returncode != 0
    assert "identity mismatch" in rejected.stderr
    assert installed_value(corrupt_stage) == old_value

    # Journal and remote staged-package symlinks are rejected without following
    # them into external state.
    symlink_journal = root / "symlink-journal"
    symlink_journal.mkdir()
    run(symlink_journal, "add", source)
    external_journal = root / "external-journal.json"
    external_journal.write_text('{"version":1}\n', encoding="utf-8")
    journal_path = symlink_journal / ".nift/package-transaction.json"
    try:
        journal_path.symlink_to(external_journal)
    except OSError:
        pass
    else:
        rejected = run(symlink_journal, "install", ok=False)
        assert rejected.returncode != 0
        assert "not a regular file" in rejected.stderr
        assert external_journal.exists()

    dangling_site = root / "dangling-journal"
    dangling_site.mkdir()
    run(dangling_site, "add", source)
    dangling = dangling_site / ".nift/package-transaction.json"
    try:
        dangling.symlink_to(root / "does-not-exist.json")
    except OSError:
        pass
    else:
        rejected = run(dangling_site, "install", ok=False)
        assert rejected.returncode != 0
        assert "not a regular file" in rejected.stderr
        assert dangling.is_symlink()

    symlink_stage = root / "symlink-stage"
    symlink_stage.mkdir()
    run(symlink_stage, "add", source)
    old_value = installed_value(symlink_stage)
    advance_repo(repo)
    crashed = run(symlink_stage, "update", "demo", env={"NIFT_TEST_PACKAGE_TXN_CRASH": "after-journal"}, ok=False)
    assert crashed.returncode == 86
    transaction_dir = next(path for path in (symlink_stage / ".nift/packages").iterdir() if path.name.startswith(".txn-"))
    staged = transaction_dir / "new/demo"
    external_stage = root / "external-stage"
    staged.rename(external_stage)
    try:
        staged.symlink_to(external_stage, target_is_directory=True)
    except OSError:
        external_stage.rename(staged)
    else:
        rejected = run(symlink_stage, "install", ok=False)
        assert rejected.returncode != 0
        assert "real directory" in rejected.stderr
        assert installed_value(symlink_stage) == old_value

    # The process-held lock serializes concurrent commands. The second command
    # observes the first command's committed metadata and fails as a duplicate,
    # rather than racing on staging or metadata files.
    concurrent = root / "concurrent"
    concurrent.mkdir()
    hold = root / "hold"
    hold.mkdir()
    env = os.environ.copy()
    env["NIFT_TEST_PACKAGE_TXN_HOLD"] = str(hold)
    first = subprocess.Popen([NIFT, "add", source], cwd=concurrent, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.time() + 10
    while not (hold / "acquired").exists() and time.time() < deadline:
        time.sleep(0.01)
    assert (hold / "acquired").exists()
    second = subprocess.Popen([NIFT, "add", source], cwd=concurrent,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(0.1)
    assert second.poll() is None
    (hold / "release").write_text("go\n", encoding="utf-8")
    first_out, first_err = first.communicate(timeout=20)
    second_out, second_err = second.communicate(timeout=20)
    assert first.returncode == 0, first_err
    assert second.returncode != 0
    assert "already a dependency" in second_err
    assert_clean(concurrent)

    # Imports hold a shared package lock for module execution and cannot race a
    # package writer's backup/promotion window.
    reader_site = root / "reader-lock"
    reader_site.mkdir()
    run(reader_site, "add", source)
    advance_repo(repo)
    reader_hold = root / "reader-hold"
    reader_hold.mkdir()
    env = os.environ.copy()
    env["NIFT_TEST_PACKAGE_TXN_HOLD"] = str(reader_hold)
    writer = subprocess.Popen([NIFT, "update", "demo"], cwd=reader_site, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.time() + 10
    while not (reader_hold / "acquired").exists() and time.time() < deadline:
        time.sleep(0.01)
    assert (reader_hold / "acquired").exists()
    (reader_site / "read.f").write_text('import("demo")\nprint(value)\n', encoding="utf-8")
    reader = subprocess.Popen([NIFT, "read.f"], cwd=reader_site,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(0.1)
    assert reader.poll() is None
    (reader_hold / "release").write_text("go\n", encoding="utf-8")
    _, writer_err = writer.communicate(timeout=20)
    reader_out, reader_err = reader.communicate(timeout=20)
    assert writer.returncode == 0, writer_err
    assert reader.returncode == 0, reader_err
    assert reader_out.strip().startswith("two-")
    assert_clean(reader_site)

    # Package owners retain frozen lock provenance, not a lifetime read lock.
    # Unchanged delayed imports and resource paths work normally.
    (repo / "src/main.f").write_text(
        'fn(unchanged_value()) { import("./helper.f"); return helper_value }\n'
        'fn(current_path()) { return module_path("asset.txt") }\n'
        'fn(delayed_path()) { sleep(2000); return module_path("worker.txt") }\n'
        'fn(delayed_import()) { sleep(2000); import("./helper.f"); return helper_value }\n'
        'export(unchanged_value)\nexport(current_path)\nexport(delayed_path)\nexport(delayed_import)\n',
        encoding="utf-8")
    (repo / "src/helper.f").write_text('helper_value := "before"\nexport(helper_value)\n', encoding="utf-8")
    git(repo, "add", ".")
    git(repo, "commit", "-qm", "provenance-base")

    def advance_helper(value):
        (repo / "src/helper.f").write_text(
            f'helper_value := "{value}"\nexport(helper_value)\n', encoding="utf-8")
        git(repo, "add", ".")
        git(repo, "commit", "-qm", value)

    unchanged = root / "unchanged-provenance"
    unchanged.mkdir()
    run(unchanged, "add", source)
    (unchanged / "same.f").write_text(
        'import("demo")\nprint(unchanged_value())\nprint(current_path())\n', encoding="utf-8")
    same = run(unchanged, "same.f")
    assert same.stdout.splitlines() == [
        "before", (unchanged / ".nift/packages/demo/src/asset.txt").as_posix()
    ]

    # A synchronous child package command must complete while an old package
    # callable remains live. The next package-owned path operation rejects the
    # changed lock provenance instead of silently using the replacement.
    advance_helper("child-update")
    (unchanged / "child-update.f").write_text(
        f'import("demo")\nr := run({json.dumps(NIFT)}, "update", "demo")\n'
        'print(r.exit_code)\nprint(current_path())\n', encoding="utf-8")
    child = run(unchanged, "child-update.f", ok=False)
    assert child.stdout.splitlines() == ["0"]
    assert "stale package ownership" in child.stderr
    assert "child-update" in (unchanged / ".nift/packages/demo/src/helper.f").read_text(encoding="utf-8")
    assert_clean(unchanged)

    def stale_worker_case(name, callable_name, next_value):
        site = root / name
        site.mkdir()
        run(site, "add", source)
        advance_helper(next_value)
        marker = site / "worker-ready"
        (site / "worker.f").write_text(
            f'import("demo")\nworker := thread({callable_name})\n'
            f'touch({json.dumps(marker.as_posix())})\nprint(worker.join())\n', encoding="utf-8")
        reader = subprocess.Popen([NIFT, "worker.f"], cwd=site,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.time() + 10
        while not marker.exists() and time.time() < deadline:
            time.sleep(0.01)
        assert marker.exists()
        writer = run(site, "update", "demo")
        assert writer.returncode == 0
        reader_out, reader_err = reader.communicate(timeout=20)
        assert reader.returncode != 0, reader_out
        assert "stale package ownership" in reader_err
        assert_clean(site)

    stale_worker_case("stale-worker-path", "delayed_path", "worker-path-update")
    stale_worker_case("stale-worker-import", "delayed_import", "worker-import-update")

    # Local package links intentionally remain live: source edits do not alter
    # lock provenance, so a delayed relative import observes the local source.
    local_source = root / "local-source"
    (local_source / "src").mkdir(parents=True)
    (local_source / "manifest.json").write_text(
        '{"name":"localdemo","version":"0.1.0","entry":"src/main.f"}\n', encoding="utf-8")
    (local_source / "src/main.f").write_text(
        'fn(delayed_local()) { sleep(500); import("./helper.f"); return helper_value }\nexport(delayed_local)\n',
        encoding="utf-8")
    (local_source / "src/helper.f").write_text(
        'helper_value := "local-before"\nexport(helper_value)\n', encoding="utf-8")
    local_site = root / "local-live"
    local_site.mkdir()
    run(local_site, "add", str(local_source))
    installed_local = local_site / ".nift/packages/localdemo"
    if installed_local.is_symlink():
        marker = local_site / "local-ready"
        (local_site / "local.f").write_text(
            f'import("localdemo")\nworker := thread(delayed_local)\n'
            f'touch({json.dumps(marker.as_posix())})\nprint(worker.join())\n', encoding="utf-8")
        reader = subprocess.Popen([NIFT, "local.f"], cwd=local_site,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.time() + 10
        while not marker.exists() and time.time() < deadline:
            time.sleep(0.01)
        assert marker.exists()
        (local_source / "src/helper.f").write_text(
            'helper_value := "local-after"\nexport(helper_value)\n', encoding="utf-8")
        local_out, local_err = reader.communicate(timeout=20)
        assert reader.returncode == 0, local_err
        assert local_out.strip() == "local-after"

print("PASS package transaction recovery and locking")
