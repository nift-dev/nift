#!/usr/bin/env python3
"""A gate leaves the repository as it found it (B4b review F2).

  repo_state.py save FILE     record the repository's state
  repo_state.py check FILE    fail (exit 1) if the state differs from FILE, listing the paths
  repo_state.py --self-test   the check catches a tracked change, an untracked and an ignored file

The state is every path `git status --porcelain --ignored=matching --untracked-files=all`
reports (tracked changes, untracked files and ignored files, each named), plus the hash of
`git diff HEAD`, so a further change to an already modified file shows too. Build products
are left out: `.build/`, object, dependency and configuration sidecar files, and the
binaries the Makefile links at the root (a gate's prerequisites build them before the
recipe saves the state, and a parallel make may still be linking others).
"""
import hashlib
import os
import subprocess
import sys
import tempfile

BUILD = (".o", ".d", ".cfg", ".obj", ".pdb", ".exe")
ROOT_BINARIES = {"nift", "libnift_c.so", "libnift_c.a"}


def state(root):
    out = subprocess.run(["git", "status", "--porcelain=v1", "--ignored=matching", "--untracked-files=all", "-z",
                          "--", ".", ":(exclude).build"], cwd=root, capture_output=True, check=True).stdout
    lines = []
    for entry in out.decode("utf-8", "replace").split("\0"):
        if not entry:
            continue
        path = entry[3:]
        if path.endswith(BUILD) or path in ROOT_BINARIES or path.startswith("minifypp/.build/"):
            continue
        lines.append(entry)
    diff = subprocess.run(["git", "diff", "HEAD", "--binary"], cwd=root, capture_output=True, check=True).stdout
    return sorted(lines) + ["diff " + hashlib.sha256(diff).hexdigest()]


def compare(before, after):
    gone, new = sorted(set(before) - set(after)), sorted(set(after) - set(before))
    return ["- " + x for x in gone] + ["+ " + x for x in new]


def self_test():
    with tempfile.TemporaryDirectory(prefix="repo-state-selftest-") as d:
        git = lambda *a: subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *a], cwd=d,
                                        capture_output=True, check=True)
        git("init", "-q")
        open(os.path.join(d, ".gitignore"), "w").write("*.log\n/.build/\n")
        open(os.path.join(d, "a.txt"), "w").write("a\n")
        git("add", "-A")
        git("commit", "-q", "-m", "init")
        os.makedirs(os.path.join(d, ".build"))
        base = state(d)
        cases = []
        open(os.path.join(d, ".build", "x"), "w").write("built\n")  # build products are left out
        cases.append(("a build product", compare(base, state(d)) == []))
        open(os.path.join(d, "sub.log"), "w").write("ignored\n")
        cases.append(("an ignored file", any("sub.log" in x for x in compare(base, state(d)))))
        os.remove(os.path.join(d, "sub.log"))
        os.makedirs(os.path.join(d, "t"))
        open(os.path.join(d, "t", "out.txt"), "w").write("untracked\n")
        cases.append(("an untracked file", any("t/out.txt" in x for x in compare(base, state(d)))))
        os.remove(os.path.join(d, "t", "out.txt"))
        open(os.path.join(d, "a.txt"), "w").write("b\n")
        changed = state(d)
        cases.append(("a tracked change", compare(base, changed) != []))
        open(os.path.join(d, "a.txt"), "w").write("c\n")
        cases.append(("a further change to a changed file", compare(changed, state(d)) != []))
        open(os.path.join(d, "a.txt"), "w").write("a\n")
        cases.append(("the restored tree", compare(base, state(d)) == []))
    failed = [name for name, ok in cases if not ok]
    for name in failed:
        print("FAIL repo-state self-test: %s" % name)
    print("%s repo-state self-test: %d cases" % ("FAIL" if failed else "PASS", len(cases)))
    return 1 if failed else 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 3 or sys.argv[1] not in ("save", "check"):
        print(__doc__)
        return 2
    root = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, check=True,
                          text=True).stdout.strip()
    if sys.argv[1] == "save":
        os.makedirs(os.path.dirname(os.path.abspath(sys.argv[2])), exist_ok=True)
        open(sys.argv[2], "w").write("\n".join(state(root)) + "\n")
        return 0
    before = open(sys.argv[2]).read().splitlines()
    changes = compare(before, state(root))
    for line in changes[:40]:
        print("FAIL repository changed by the gate: %s" % line)
    print("%s repo-state: %d change(s)" % ("FAIL" if changes else "PASS", len(changes)))
    return 1 if changes else 0


if __name__ == "__main__":
    sys.exit(main())
