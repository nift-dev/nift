#!/usr/bin/env python3
"""CP13B Phase B mutant build runner: isolated, clean, identity-checked.

  mutant_build.py --root . --dry-run
      Build the unmodified tree in a clean temporary worktree/copy. A non-zero
      build exit or missing binary is a gate failure, never a detection.

  mutant_build.py --root . --mutant N --output /tmp/nift-exc-N \
                  [--expect-marker exc-mutant=N]
      Build a clean tree with -DNIFT_EXC_MUTANT=N in a separate directory,
      copy the binary, record its sha256 and the defining flag, and optionally
      check that `nift --version` contains the Phase B marker. No object from
      the source tree or another mutant is reused.

In Phase A no exception hooks exist; --mutant is a dry-run convention only and
must never be credited as mutation evidence. The mutation gate's baseline
future check prevents that.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_CXXFLAGS = "-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
EXCLUDE = {".git", ".build", "nift", "__pycache__"}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def is_git_tree(root):
    return (root / ".git").exists() and shutil.which("git") is not None


def isolated_tree(root):
    """Return (path, cleanup). A clean git worktree or a filtered copy."""
    if is_git_tree(root):
        tmp = Path(tempfile.mkdtemp(prefix="cp13b-mutant-build-"))
        proc = subprocess.run(["git", "-C", str(root), "worktree", "add", "--detach",
                               str(tmp), "HEAD"], capture_output=True, text=True)
        if proc.returncode != 0:
            shutil.rmtree(tmp, ignore_errors=True)
            raise RuntimeError(f"git worktree add failed: {proc.stderr.strip()}")
        def cleanup():
            removed = subprocess.run(["git", "-C", str(root), "worktree", "remove", "--force", str(tmp)],
                                     capture_output=True)
            if removed.returncode != 0:
                print(f"warning: worktree cleanup exited {removed.returncode}")
            shutil.rmtree(tmp, ignore_errors=True)
        return tmp, cleanup
    tmp = Path(tempfile.mkdtemp(prefix="cp13b-mutant-build-"))
    ignore = shutil.ignore_patterns(*EXCLUDE)
    shutil.copytree(root, tmp / "tree", ignore=ignore)
    return tmp / "tree", lambda: shutil.rmtree(tmp, ignore_errors=True)


def build(root, cxxflags=None):
    cmd = ["make", "-j", str(max(1, os.cpu_count() or 1)), "nift"]
    env = dict(os.environ)
    if cxxflags:
        env["CXXFLAGS"] = cxxflags
    print("+", " ".join(cmd), "(CXXFLAGS=%s)" % (cxxflags or "$default"))
    try:
        return subprocess.run(cmd, cwd=root, env=env).returncode
    except OSError as error:
        print(f"FAIL: cannot run make: {error}")
        return 127


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--mutant", type=int, default=None)
    ap.add_argument("--output", default=None)
    ap.add_argument("--expect-marker", default=None)
    args = ap.parse_args()
    root = Path(args.root).resolve()
    if not args.dry_run and args.mutant is None:
        print("FAIL: --mutant N or --dry-run is required", file=sys.stderr)
        return 2
    flags = None
    if args.mutant is not None:
        flags = f"{os.environ.get('CXXFLAGS', DEFAULT_CXXFLAGS)} -DNIFT_EXC_MUTANT={args.mutant}"
    tree, cleanup = isolated_tree(root)
    try:
        status = build(tree, flags)
        binary = tree / "nift"
        if status != 0:
            print(f"FAIL: build exited {status} in an isolated tree "
                  "(a build failure is not a detection)")
            return 1
        if not binary.exists():
            print("FAIL: the build produced no nift binary")
            return 1
        digest = sha256(binary)
        head = subprocess.run(["git", "-C", str(tree), "rev-parse", "HEAD"],
                              capture_output=True, text=True)
        commit = head.stdout.strip() if head.returncode == 0 else "no-git"
        if args.dry_run:
            print(f"PASS mutant build dry run: clean {commit} nift sha256={digest}")
            return 0
        if args.expect_marker:
            version = subprocess.run([str(binary), "--version"], capture_output=True, text=True)
            text = version.stdout + version.stderr
            if args.expect_marker not in text:
                print(f"FAIL: mutant {args.mutant} binary does not report {args.expect_marker!r}; "
                      "the mutation may not have been compiled")
                return 1
        target = Path(args.output) if args.output else (root / f"nift-exc-{args.mutant}")
        shutil.copy2(binary, target)
        print(f"built mutant {args.mutant} from clean {commit}: {target} sha256={digest} "
              f"define=-DNIFT_EXC_MUTANT={args.mutant}")
        return 0
    except RuntimeError as error:
        print(f"FAIL: {error}")
        return 1
    finally:
        cleanup()


if __name__ == "__main__":
    sys.exit(main())
