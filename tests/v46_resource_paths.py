#!/usr/bin/env python3
"""Batch 3 module_path/package_path CLI, package, and worker contract."""

import os
import json
import pathlib
import subprocess
import sys
import tempfile


NIFT = pathlib.Path(sys.argv[1]).resolve()


def run(args, cwd, *, source=None, ok=True, env=None):
    result = subprocess.run(
        [str(NIFT), *args], cwd=cwd, input=source, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
    )
    if ok and result.returncode != 0:
        raise AssertionError(f"command failed: {args}\n{result.stderr}")
    if not ok and result.returncode == 0:
        raise AssertionError(f"command unexpectedly succeeded: {args}\n{result.stdout}")
    return result


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


with tempfile.TemporaryDirectory() as temporary:
    root = pathlib.Path(temporary).resolve()
    local = root / "local"
    nested = local / "modules" / "nested"
    nested.mkdir(parents=True)
    write(nested / "child.f", """
fn(child_dir()) { return module_path() }
fn(child_missing()) { return module_path("missing/leaf.txt") }
export(child_dir)
export(child_missing)
""")
    write(local / "main.f", """
print(module_path())
print(module_path("missing/leaf.txt"))
import("./modules/nested/child.f")
print(child_dir())
print(child_missing())
print(module_path("../ordinary-authority.txt"))
""")
    result = run(["main.f"], local)
    assert result.stdout.splitlines() == [
        local.as_posix(),
        (local / "missing" / "leaf.txt").as_posix(),
        nested.as_posix(),
        (nested / "missing" / "leaf.txt").as_posix(),
        (root / "ordinary-authority.txt").as_posix(),
    ]

    site = root / "site"
    package = site / ".nift" / "packages" / "paths"
    source_dir = package / "src"
    child_dir = source_dir / "nested"
    child_dir.mkdir(parents=True)
    package_source = package.as_posix()
    write(site / "manifest.json", json.dumps({
        "dependencies": {"paths": {"source": package_source, "ref": "local"}}
    }) + "\n")
    write(site / ".nift" / "packages.lock.json", json.dumps({
        "paths": {"source": package_source, "requested": "local", "commit": "local"}
    }) + "\n")
    write(package / "manifest.json", '{"name":"paths","version":"0.1.0","entry":"src/main.f"}\n')
    write(child_dir / "child.f", """
fn(nested_path()) { return module_path() }
fn(nested_package()) { return package_path("nested-resource.txt") }
export(nested_path)
export(nested_package)
""")
    write(source_dir / "main.f", """
top_module := module_path()
top_package := package_path()
fn(paths()) { return [module_path(), module_path("missing/leaf.txt"), package_path(), package_path("new/parent/file.txt")] }
fn(resolve_module(p)) { return module_path(p) }
fn(resolve_package(p)) { return package_path(p) }
fn(callback_path(x)) { return module_path(x) }
callback := callback_path
path_lambda := (x) => module_path(x)
future_path := async () => module_path("future.txt")
fn(worker_path()) { return module_path("worker.txt") }
struct(locator) { fn(path()) { return module_path("method.txt") } }
instance := locator()
import("./nested/child.f")
export(top_module)
export(top_package)
export(paths)
export(resolve_module)
export(resolve_package)
export(callback)
export(path_lambda)
export(future_path)
export(worker_path)
export(locator)
export(instance)
export(nested_path)
export(nested_package)
""")
    write(site / "cwd" / "cwd.txt", "cwd-semantics\n")
    write(site / "main.f", """
import("paths")
print(top_module)
print(top_package)
resolved := paths()
print(resolved[0])
print(resolved[1])
print(resolved[2])
print(resolved[3])
print(instance.path())
print(path_lambda("lambda.txt"))
print(["callback.txt"].map(callback)[0])
print(nested_path())
print(nested_package())
w := thread(worker_path)
print(w.join())
future := future_path()
print(await future)
cd("cwd")
print(paths()[0])
print(open("cwd.txt").trim())
""")
    result = run(["main.f"], site)
    assert result.stdout.splitlines() == [
        source_dir.as_posix(), package.as_posix(), source_dir.as_posix(),
        (source_dir / "missing" / "leaf.txt").as_posix(), package.as_posix(),
        (package / "new" / "parent" / "file.txt").as_posix(),
        (source_dir / "method.txt").as_posix(),
        (source_dir / "lambda.txt").as_posix(),
        (source_dir / "callback.txt").as_posix(), child_dir.as_posix(),
        (package / "nested-resource.txt").as_posix(),
        (source_dir / "worker.txt").as_posix(), (source_dir / "future.txt").as_posix(),
        source_dir.as_posix(), "cwd-semantics",
    ]

    def package_failure(expression, diagnostic):
        write(site / "failure.f", f'import("paths")\nprint({expression})\n')
        failed = run(["failure.f"], site, ok=False)
        assert diagnostic in failed.stderr, failed.stderr

    package_failure('resolve_package("")', "relative path must not be empty")
    package_failure('resolve_package("../escape")', "path must stay inside the owning package")
    package_failure('resolve_module("../../escape")', "path must stay inside the owning package")
    package_failure('resolve_package("/absolute")', "path must be relative and not root-qualified")
    package_failure('resolve_package("C:\\\\absolute")', "path must be relative and not root-qualified")
    package_failure('resolve_package("\\\\\\\\server\\\\share")', "path must be relative and not root-qualified")
    package_failure('resolve_package("\\\\rooted")', "path must be relative and not root-qualified")
    package_failure('resolve_package(1)', "expected string path")
    package_failure('package_path()', "current module is not owned by a package")
    write(site / "arity.f", 'print(module_path("one", "two"))\n')
    assert "expected zero or one relative path" in run(["arity.f"], site, ok=False).stderr

    outside = root / "outside"
    outside.mkdir()
    symlink = package / "escape-link"
    try:
        symlink.symlink_to(outside, target_is_directory=True)
    except OSError:
        print("  (package symlink escape skipped: native symlinks unavailable)")
    else:
        package_failure('resolve_package("escape-link/file.txt")', "path must stay inside the owning package")
        package_failure('resolve_module("../escape-link/file.txt")', "path must stay inside the owning package")

    for args, source in [(["-e", "print(module_path())"], None), (["eval", "module_path()"], None), (["-"], "print(module_path())\n")]:
        failed = run(args, site, source=source, ok=False)
        assert "no file-backed source" in failed.stderr, failed.stderr
    repl = run([], site, source="module_path()\nexit\n")
    assert "no file-backed source" in repl.stderr, repl.stderr

    restricted = os.environ.copy()
    restricted["NIFT_FS_ROOT"] = str(local)
    write(local / "restricted.f", 'print(module_path("../escape"))\n')
    failed = run(["restricted.f"], local, ok=False, env=restricted)
    assert "path escapes configured filesystem root" in failed.stderr

    write(local / "authority.txt", "anchored\n")
    write(local / "snapshot.f",
          'fn(worker_authority()) { return [module_path(), open("../authority.txt").trim()] }\n'
          'cd("modules")\nprint(module_path())\nprint(open("../authority.txt").trim())\n'
          'authority_worker := thread(worker_authority)\nworker_result := authority_worker.join()\n'
          'print(worker_result[0])\nprint(worker_result[1])\n')
    anchored = run(["snapshot.f", "--fs-root=."], local)
    assert anchored.stdout.splitlines() == [local.as_posix(), "anchored", local.as_posix(), "anchored"]

print("PASS v4.6 explicit module/package resource paths")
