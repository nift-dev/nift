#!/usr/bin/env python3
"""Package imports and import-time program errors on the VM (D-NEXT-packages;
docs/runtime-2/checkpoints/weekend-packages.md). Every case runs the VM (the
default) against legacy (NIFT_VM=0) in fresh copies of its workspace.

  A  package resolution: every step of contract §2, legacy's text, position and
     exit status; the VM runs nothing before a resolution error (approved
     difference: legacy runs the statements before the import)
  B  module program errors (Q6, C3): return <value>, export-missing,
     export-duplicate, export(<non-name>); depth 1 and 2
  C  pre-effect: earlier statements and earlier imported modules have no effect
  D  competing failures: legacy's first-executed order; program errors over
     feature refusals; the early-`return` note; determinism under permutation
  E  the package lock: housekeeping only (Q2), adversarial tests 1 and 2
  F  args/cmd in modules (Q4/Q5, C1): identity, the private binding's rename
  G  runtime prefixes in each import's own spelling; legacy package texts kept

Usage: packages.py [--nift ./nift]   (exit 1 on any failed check)
"""
import argparse
import fcntl
import itertools
import json
import os
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
FAILS = []
COUNT = [0]
NIFT = None
WORK = None
NOTE = "note: the program was not executed: its imports are resolved before it runs, so no part of it ran"


def check(ok, label, detail=""):
    COUNT[0] += 1
    if not ok:
        FAILS.append(label)
        print("FAIL " + label + (": " + str(detail)[:600] if detail else ""))


def env_for(mode, extra=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(HOME=str(WORK / "home"), LANG="C.UTF-8", NO_COLOR="1")
    if mode is not None:
        env["NIFT_VM"] = mode
    env.update(extra or {})
    return env


def run(argv, cwd, mode=None, extra=None, timeout=60):
    stats = WORK / ("stats-%d.jsonl" % os.getpid())
    stats.unlink(missing_ok=True)
    env = env_for(mode, extra)
    env["NIFT_VM_STATS"] = str(stats)
    p = subprocess.run([NIFT, *argv], cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout)
    rows = [json.loads(l) for l in stats.read_text().splitlines() if l.strip()] if stats.exists() else []
    stats.unlink(missing_ok=True)
    return p, rows


def tree(files, base=None):
    """A fresh workspace from {relative path: text} (None: a directory)."""
    root = Path(tempfile.mkdtemp(dir=base or WORK))
    for rel, text in files.items():
        path = root / rel
        if text is None:
            path.mkdir(parents=True, exist_ok=True)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    return root


def snapshot(root):
    out = {}
    for dirpath, dirnames, filenames in os.walk(root):
        for name in dirnames + filenames:
            p = Path(dirpath) / name
            rel = str(p.relative_to(root))
            out[rel] = p.read_bytes() if p.is_file() and not p.is_symlink() else None
    return out


def error_line(text):
    return next((l for l in text.splitlines() if l.startswith("error: ")), "")


def package(name, main, lock=True, extra=None, commit="local"):
    files = {".nift/packages/%s/manifest.json" % name:
             '{"name":"%s","version":"0.1.0","entry":"src/main.f"}\n' % name,
             ".nift/packages/%s/src/main.f" % name: main}
    if lock:
        files[".nift/packages.lock.json"] = json.dumps(
            {name: {"source": "./" + name, "requested": "local", "commit": commit}})
    files.update(extra or {})
    return files


def lock_all(names):
    return json.dumps({n: {"source": "./" + n, "requested": "local", "commit": "local"} for n in names})


def same_error_as_legacy(label, files, script="m.f", cwd=".", reason=None, notes=(), args=()):
    """Program error parity: legacy's error line (text and file:L:C), exit 1;
    the VM prints nothing before it and adds the not-executed note."""
    vm_root = tree(files)
    lg_root = tree(files)
    before = snapshot(vm_root)
    vm, rows = run([script, *args], vm_root / cwd)
    after = snapshot(vm_root)
    legacy, _ = run([script, *args], lg_root / cwd, "0")
    vline = error_line(vm.stderr).replace(str(vm_root), "R")
    lline = error_line(legacy.stderr).replace(str(lg_root), "R")
    check(vm.returncode == 1 and legacy.returncode == 1, label + ": exit 1 in both",
          repr((vm.returncode, legacy.returncode, vm.stderr, legacy.stderr)))
    check(vline == lline and vline, label + ": legacy's text and position", repr((vline, lline)))
    check(vm.stdout == "", label + ": nothing ran on the VM", repr(vm.stdout))
    check(NOTE in vm.stderr, label + ": not-executed note", vm.stderr)
    check("not supported" not in vm.stderr and "not compiled for the VM" not in vm.stderr and
          "no exact position" not in vm.stderr, label + ": a program error, not a missing feature", vm.stderr)
    check(not any(r.get("vm_executions") for r in rows), label + ": nothing executed", repr(rows))
    if reason:
        check(any(r.get("reason") == reason for r in rows), label + ": reason id " + reason, repr(rows))
    created = sorted(set(after) - set(before) - {".nift/.packages-transaction.lock"})
    changed = [k for k in before if before[k] != after.get(k)]
    check(not created and not changed, label + ": no user-program effect", repr((created, changed)))
    for n in notes:
        check(n in vm.stderr.replace(str(vm_root), "R"), label + ": note " + n, vm.stderr)
    shutil.rmtree(vm_root)
    shutil.rmtree(lg_root)
    return vm, legacy


def genuine(label, files, reason, at, text=None, script="m.f", cwd=".", args=(), program_error=True):
    """Owner diagnostic policy (contract §4.6): the reported error is genuine and classified (`reason`), at a
    recorded position (`at`, R-relative), with a meaningful message, rejected before any user-program effect.
    Which of several genuine errors is reported follows the VM's documented order, not legacy's."""
    root = tree(files)
    before = snapshot(root)
    vm, rows = run([script, *args], root / cwd)
    after = snapshot(root)
    line = error_line(vm.stderr).replace(str(root), "R")
    check(vm.returncode == 1 and vm.stdout == "", label + ": rejected, exit 1, no output", repr((vm.returncode, vm.stderr)))
    check(any(r.get("reason") == reason for r in rows), label + ": reason " + reason, repr((rows, vm.stderr)))
    check(line.startswith("error: " + at + ":"), label + ": at " + at, line)
    if text:
        check(text in line, label + ": message", line)
    check("no exact position" not in vm.stderr, label + ": a recorded position", vm.stderr)
    if program_error:
        check("not supported" not in vm.stderr and "not compiled for the VM" not in vm.stderr,
              label + ": a program error, not a missing feature", vm.stderr)
    check(not any(r.get("vm_executions") for r in rows), label + ": nothing executed", repr(rows))
    created = sorted(set(after) - set(before) - {".nift/.packages-transaction.lock"})
    check(not created and all(before[k] == after.get(k) for k in before), label + ": no user-program effect",
          repr(created))
    shutil.rmtree(root)
    return vm


def parity(label, files, script="m.f", cwd=".", args=(), expect=None):
    vm_root = tree(files)
    lg_root = tree(files)
    vm, rows = run([script, *args], vm_root / cwd)
    legacy, _ = run([script, *args], lg_root / cwd, "0")
    norm = lambda p, r: (p.returncode, p.stdout.replace(str(r), "R"), p.stderr.replace(str(r), "R"))
    check(norm(vm, vm_root) == norm(legacy, lg_root), label + ": same as NIFT_VM=0",
          repr((norm(vm, vm_root), norm(legacy, lg_root))))
    check(bool(rows) and all(r.get("vm_executions") == 1 for r in rows), label + ": ran on the VM", repr(rows))
    if expect is not None:
        check(vm.stdout == expect, label + ": output", repr(vm.stdout))
    shutil.rmtree(vm_root)
    shutil.rmtree(lg_root)
    return vm


def refused(label, files, reason, at=None, script="m.f", cwd=".", args=()):
    """A feature refusal (or a program-error refusal) before any effect."""
    root = tree(files)
    before = snapshot(root)
    vm, rows = run([script, *args], root / cwd)
    after = snapshot(root)
    check(vm.returncode == 1 and vm.stdout == "", label + ": refused, exit 1, no output",
          repr((vm.returncode, vm.stdout, vm.stderr)))
    check(any(r.get("reason") == reason for r in rows), label + ": reason " + reason, repr((rows, vm.stderr)))
    if at:
        check((at + ":") in vm.stderr.replace(str(root) + "/", ""), label + ": at " + at, vm.stderr)
    check("no exact position" not in vm.stderr, label + ": a real position", vm.stderr)
    check(not any(r.get("vm_executions") for r in rows), label + ": nothing executed", repr(rows))
    created = sorted(set(after) - set(before) - {".nift/.packages-transaction.lock"})
    check(not created, label + ": no file created", repr(created))
    shutil.rmtree(root)
    return vm


# Statements with effects before the failing import (legacy runs them).
EARLY = 'print("before")\n'


def section_a():
    """Package resolution (contract §2, §4.1, §4.2)."""
    demo = 'fn(demo_v()) { return "demo" }\nexport(demo_v)\n'
    parity("A package found", {**package("demo", demo), "m.f": 'import("demo")\nprint(demo_v())\n'},
           expect="demo\n")
    parity("A @import package", {**package("demo", demo), "m.f": '@import("demo")\nprint(demo_v())\n'},
           expect="demo\n")
    parity("A package from the project root, script in a subdirectory",
           {**package("demo", demo), "sub/s.f": 'import("demo")\nprint(demo_v())\n'}, script="sub/s.f",
           expect="demo\n")
    same_error_as_legacy("A cwd is the project, not the script's directory",
                         {**package("demo", demo), "sub/s.f": EARLY + 'import("demo")\n'}, script="s.f", cwd="sub",
                         reason="import-error:package")
    parity("A package importing a package",
           {**package("outer", 'import("inner")\nfn(o()) { return inner_v() }\nexport(o)\n'),
            ".nift/packages/inner/manifest.json": '{"name":"inner","version":"0.1.0","entry":"src/main.f"}',
            ".nift/packages/inner/src/main.f": 'fn(inner_v()) { return "inner" }\nexport(inner_v)\n',
            ".nift/packages.lock.json": lock_all(["outer", "inner"]),
            "m.f": 'import("outer")\nprint(o())\n'}, expect="inner\n")
    parity("A package-relative helper",
           {**package("demo", 'import("./helper.f")\nexport(h)\n'),
            ".nift/packages/demo/src/helper.f": 'fn(h()) { return "helper" }\nexport(h)\n',
            "m.f": 'import("demo")\nprint(h())\n'}, expect="helper\n")
    root = tree({**package("demo", "PLACEHOLDER"), "abs.f": 'fn(abs_v()) { return "abs" }\nexport(abs_v)\n',
                 "m.f": 'import("demo")\nprint(abs_v())\n'})
    (root / ".nift/packages/demo/src/main.f").write_text('import("%s/abs.f")\nexport(abs_v)\n' % root)
    vm, rows = run(["m.f"], root)
    legacy, _ = run(["m.f"], root, "0")
    check(vm.stdout == legacy.stdout == "abs\n" and vm.returncode == 0, "A absolute import from a package",
          repr((vm.stdout, legacy.stdout, vm.stderr)))
    shutil.rmtree(root)
    errors = [
        ("A invalid name", {"m.f": EARLY + 'import("Demo")\n'}),
        ("A invalid name (dash)", {"m.f": EARLY + 'import("-x")\n'}),
        ("A empty name", {"m.f": EARLY + 'import("")\n'}),
        ("A not installed (no .nift)", {"m.f": EARLY + 'import("demo")\n'}),
        ("A not installed (store)", {".nift/packages": None, "m.f": EARLY + 'import("demo")\n'}),
        ("A lock file absent (legacy's text: file does not exist)",
         {**package("demo", demo, lock=False), "m.f": EARLY + 'import("demo")\n'}),
        ("A lock lacks the package", {**package("demo", demo, lock=False), ".nift/packages.lock.json": lock_all(["x"]),
                                      "m.f": EARLY + 'import("demo")\n'}),
        ("A name mismatch", {**package("demo", demo), ".nift/packages/demo/manifest.json":
                             '{"name":"other","version":"0.1.0","entry":"src/main.f"}', "m.f": EARLY + 'import("demo")\n'}),
        ("A manifest invalid", {**package("demo", demo), ".nift/packages/demo/manifest.json": '{"name":"demo"',
                                "m.f": EARLY + 'import("demo")\n'}),
        ("A entry uncontained", {**package("demo", demo), ".nift/packages/demo/manifest.json":
                                 '{"name":"demo","version":"0.1.0","entry":"../x.f"}', "m.f": EARLY + 'import("demo")\n'}),
        ("A entry missing", {**package("demo", demo), ".nift/packages/demo/manifest.json":
                             '{"name":"demo","version":"0.1.0","entry":"src/none.f"}', "m.f": EARLY + 'import("demo")\n'}),
        ("A invalid exact commit", {**package("demo", demo, commit="abc"), "m.f": EARLY + 'import("demo")\n'}),
        ("A revision mismatch", {**package("demo", demo, commit="a" * 40), ".nift/packages/demo/.git/HEAD": "b" * 40 + "\n",
                                 "m.f": EARLY + 'import("demo")\n'}),
        ("A package in a package not installed",
         {**package("outer", 'import("inner")\nfn(o()) { return 1 }\nexport(o)\n'), "m.f": EARLY + 'import("outer")\n'}),
        ("A package-relative ../ escape", {**package("demo", 'import("../../../../x.f")\nexport(o)\n'),
                                           "x.f": 'fn(o()) { return 1 }\nexport(o)\n', "m.f": EARLY + 'import("demo")\n'}),
        ("A missing module", {"m.f": EARLY + 'import("missing.f")\n'}),
        ("A missing module two levels down", {"mid.f": 'import("missing.f")\n', "m.f": EARLY + 'import("mid.f")\n'}),
        ("A cycle", {"a.f": 'import("b.f")\n', "b.f": 'import("a.f")\n', "m.f": EARLY + 'import("a.f")\n'}),
        ("A self cycle", {"s.f": 'import("s.f")\n', "m.f": EARLY + 'import("s.f")\n'}),
        ("A mixed frames @import then import", {"mx.f": 'import("missing.f")\n', "m.f": EARLY + '@import("mx.f")\n'}),
        ("A mixed frames import then @import", {"my.f": '@import("missing.f")\n', "m.f": EARLY + 'import("my.f")\n'}),
        ("A journal present", {**package("demo", demo), ".nift/package-transaction.json": "{}",
                               "m.f": EARLY + 'import("demo")\n'}),
    ]
    ids = {"A missing module": "import-error:missing", "A cycle": "import-error:cycle",
           "A package-relative ../ escape": "import-error:package-escape", "A journal present": "import-error:package-lock",
           "A not installed (no .nift)": "import-error:package", "A lock file absent (legacy's text: file does not exist)":
           "import-error:package"}
    for label, files in errors:
        same_error_as_legacy(label, files, reason=ids.get(label))
    # The package-relative symlink escape (POSIX).
    root = tree({**package("demo", 'import("./link.f")\nexport(o)\n'), "outside.f": 'fn(o()) { return 1 }\nexport(o)\n',
                 "m.f": EARLY + 'import("demo")\n'})
    os.symlink("../../../../outside.f", root / ".nift/packages/demo/src/link.f")
    lg = Path(tempfile.mkdtemp(dir=WORK))
    shutil.rmtree(lg)
    shutil.copytree(root, lg, symlinks=True)
    vm, rows = run(["m.f"], root)
    legacy, _ = run(["m.f"], lg, "0")
    check(error_line(vm.stderr).replace(str(root), "R") == error_line(legacy.stderr).replace(str(lg), "R") and
          "escapes package root" in vm.stderr and vm.stdout == "" and vm.returncode == 1,
          "A package-relative symlink escape", repr((vm.stderr, legacy.stderr)))
    check(any(r.get("reason") == "import-error:package-escape" for r in rows), "A symlink escape reason id", repr(rows))
    shutil.rmtree(root)
    shutil.rmtree(lg)
    # The store itself a symlink outside the project.
    root = tree({**{"outside/" + k: v for k, v in package("demo", demo).items()}, "p/m.f": EARLY + 'import("demo")\n'})
    (root / "p/.nift").mkdir()
    os.symlink(root / "outside/.nift/packages", root / "p/.nift/packages")
    shutil.copy(root / "outside/.nift/packages.lock.json", root / "p/.nift/packages.lock.json")
    vm, _ = run(["m.f"], root / "p")
    legacy, _ = run(["m.f"], root / "p", "0")
    check(error_line(vm.stderr) == error_line(legacy.stderr) and "package store must stay inside the project" in vm.stderr
          and vm.stdout == "", "A store symlinked outside the project", repr((vm.stderr, legacy.stderr)))
    shutil.rmtree(root)
    # Legacy's texts are kept (Q3), including the pathless one.
    vm = same_error_as_legacy("A legacy text kept", {**package("demo", demo, lock=False), "m.f": 'import("demo")\n'})[0]
    check(vm.stderr.splitlines()[0].endswith(": import: file does not exist"), "A pathless legacy text", vm.stderr)


def section_b():
    """Module program errors (Q6, C3)."""
    cases = [
        ("B export missing", {"e.f": 'export(nope)\n', "m.f": EARLY + 'import("e.f")\n'}, "module-error:export-missing", []),
        ("B export missing after a function", {"e.f": 'fn(g()) { return 2 }\nexport(g)\nexport(nope)\n',
                                               "m.f": EARLY + 'import("e.f")\n'}, "module-error:export-missing", []),
        ("B export before the function (legacy does not hoist)", {"e.f": 'export(f)\nfn(f()) { return 1 }\n',
                                                                  "m.f": EARLY + 'import("e.f")\n'},
         "module-error:export-missing", []),
        ("B export of a block binding", {"e.f": 'if (true) { y := 1 }\nexport(y)\n', "m.f": EARLY + 'import("e.f")\n'},
         "module-error:export-missing", []),
        ("B export duplicate", {"e.f": 'x := 1\nexport(x)\nexport(x)\n', "m.f": EARLY + 'import("e.f")\n'},
         "module-error:export-duplicate", []),
        ("B export missing, depth 2", {"e.f": 'export(nope)\n', "mid.f": 'import("e.f")\n', "m.f": EARLY + 'import("mid.f")\n'},
         "module-error:export-missing", []),
    ]
    for arg in ("1", '"x"', "f()", "o.x", "x + 1", "() => 1"):
        cases.append(("B export(%s)" % arg, {"e.f": 'x := 1\no := {"x": 1}\nfn(f()) { return 1 }\nexport(%s)\n' % arg,
                                            "m.f": EARLY + 'import("e.f")\n'}, "module-error:export-name", []))
        cases.append(("B export(%s), depth 2" % arg, {"e.f": 'x := 1\nexport(%s)\n' % arg, "mid.f": 'import("e.f")\n',
                                                     "m.f": EARLY + 'import("mid.f")\n'}, "module-error:export-name", []))
    for label, files, reason, notes in cases:
        same_error_as_legacy(label, files, reason=reason, notes=notes)
    # `return <value>` in a module: reported at the `return` itself (owner policy: the VM's own position).
    genuine("B return value", {"r.f": 'x := 1\nreturn 5\n', "m.f": EARLY + 'import("r.f")\n'},
            "module-error:return-value", "R/r.f:2:1", "import: return with a value is not allowed in import")
    genuine("B return value, depth 2", {"r.f": 'return 5\n', "mid.f": 'import("r.f")\n', "m.f": EARLY + 'import("mid.f")\n'},
            "module-error:return-value", "R/r.f:1:1", "import: import: return with a value is not allowed in import")
    genuine("B return value via @import", {"r.f": 'return 5\n', "m.f": EARLY + '@import("r.f")\n'},
            "module-error:return-value", "R/r.f:1:1", "@import: return with a value is not allowed in @import")
    # Valid exports are unaffected.
    parity("B valid exports", {"e.f": 'x := 1\nfn(f()) { return x + 1 }\nimport("g.f")\nexport(x)\nexport(f)\nexport(gv)\n',
                               "g.f": 'gv := 7\nexport(gv)\n', "m.f": 'import("e.f")\nprint(x)\nprint(f())\nprint(gv)\n'},
           expect="1\n2\n7\n")
    # A bare `return` is legal in legacy: still a feature refusal, not a program error.
    refused("B bare return stays a feature refusal", {"b.f": 'x := 1\nreturn\nexport(x)\n',
                                                      "m.f": 'import("b.f")\nprint(1)\n'}, "module-bare-return", "b.f:2:1")
    # A `return <value>` in an untaken branch: the static rule (approved difference); legacy runs it.
    files = {"c.f": 'if (false) { return 5 }\nx := 1\nexport(x)\n', "m.f": 'import("c.f")\nprint(x)\n'}
    root = tree(files)
    vm, rows = run(["m.f"], root)
    legacy, _ = run(["m.f"], root, "0")
    check(legacy.returncode == 0 and vm.returncode == 1 and "return with a value is not allowed in import" in vm.stderr
          and "R/c.f:1:14:" in vm.stderr.replace(str(root), "R") and vm.stdout == "",
          "B return in an untaken branch: rejected by the static rule (recorded difference)",
          repr((legacy.returncode, vm.stderr)))
    shutil.rmtree(root)


def section_c():
    """Pre-effect: nothing of the user program runs, imported modules included."""
    for label, failing in (("missing package", 'import("demo")\n'), ("missing file", 'import("missing.f")\n'),
                           ("cycle", 'import("a.f")\n'), ("export-missing", 'import("e.f")\n'),
                           ("return value", 'import("r.f")\n'), ("export(1)", 'import("n.f")\n')):
        files = {"ok.f": 'print("ok module ran")\nv := 1\nexport(v)\n',
                 "a.f": 'import("b.f")\n', "b.f": 'import("a.f")\n', "e.f": 'export(nope)\n', "r.f": 'return 1\n',
                 "n.f": 'export(1)\n',
                 "m.f": 'print("entry ran")\ntouch("marker.txt")\nimport("ok.f")\n' + failing}
        if label == "return value":  # reported at the `return` (owner policy), so not legacy's position
            genuine("C return value after effects and a loaded module", files, "module-error:return-value", "R/r.f:1:1")
            legacy, _ = run(["m.f"], tree(files), "0")
        else:
            vm, legacy = same_error_as_legacy("C " + label + " after effects and a loaded module", files)
        check("entry ran" in legacy.stdout and "ok module ran" in legacy.stdout,
              "C " + label + ": control: legacy ran the earlier statements and module", repr(legacy.stdout))
        root = tree(files)
        run(["m.f"], root, "0")
        check((root / "marker.txt").exists(), "C " + label + ": control: legacy wrote the marker")
        shutil.rmtree(root)


def section_d():
    """Competing failures and the determinism rule."""
    # A missing import before `return 5`: the missing import (legacy order).
    same_error_as_legacy("D missing import before return", {"c.f": 'import("missing.f")\nreturn 5\n',
                                                            "m.f": EARLY + 'import("c.f")\n'}, reason="import-error:missing")
    genuine("D return before missing import", {"c.f": 'return 5\nimport("missing.f")\n', "m.f": 'import("c.f")\n'},
            "module-error:return-value", "R/c.f:1:1")
    same_error_as_legacy("D two missing imports", {"m.f": EARLY + 'import("one.f")\nimport("two.f")\n'},
                         reason="import-error:missing")
    same_error_as_legacy("D export(1) in A, missing import in B",
                         {"a.f": 'export(1)\n', "b.f": 'import("missing.f")\n', "m.f": 'import("a.f")\nimport("b.f")\n'},
                         reason="module-error:export-name")
    same_error_as_legacy("D missing import in B first",
                         {"a.f": 'export(1)\n', "b.f": 'import("missing.f")\n', "m.f": 'import("b.f")\nimport("a.f")\n'},
                         reason="import-error:missing")
    # A feature refusal found first by the walk is reported (owner policy: the walk stops at the first problem).
    genuine("D feature refusal found first", {"m.f": 'fn(load()) { import("./x.f"); return 1 }\nimport("missing.f")\n'},
            "import-nested", "R/m.f:1:14", program_error=False)
    # The early-`return` note (legacy exits 0 before the import).
    files = {"m.f": 'print("b")\nreturn\nimport("missing.f")\n'}
    root = tree(files)
    vm, rows = run(["m.f"], root)
    legacy, _ = run(["m.f"], root, "0")
    st = vm.stderr.replace(str(root), "R")
    check(legacy.returncode == 0 and vm.returncode == 1 and vm.stdout == "" and
          "error: R/m.f:3:1: import: script is not readable: R/missing.f" in st and "not supported" not in st,
          "D early return: the import is rejected with its real reason (recorded difference)", repr((legacy.returncode, st)))
    shutil.rmtree(root)
    # Determinism: the same diagnostic whatever the creation order, directory
    # order or name lengths (hash bucketing).
    for names in (("a", "b", "c"), ("alpha_long_name", "b", "cc"), ("zz", "y_y", "x")):
        a, b, c = names
        files = {a + ".f": 'export(1)\n', b + ".f": 'import("missing.f")\n', c + ".f": 'x := 1\nexport(nope)\n',
                 "m.f": 'import("%s.f")\nimport("%s.f")\nimport("%s.f")\n' % (c, a, b)}
        seen = set()
        for order in itertools.permutations(list(files)):
            root = Path(tempfile.mkdtemp(dir=WORK))
            for rel in order:
                (root / rel).write_text(files[rel])
            vm, rows = run(["m.f"], root)
            seen.add((error_line(vm.stderr).replace(str(root), "R"), tuple(r.get("reason") for r in rows)))
            shutil.rmtree(root)
        root = tree(files)
        legacy, _ = run(["m.f"], root, "0")
        lline = error_line(legacy.stderr).replace(str(root), "R")
        shutil.rmtree(root)
        check(len(seen) == 1 and next(iter(seen))[0] == lline,
              "D determinism over %d creation orders (%s)" % (6 * 4, ",".join(names)), repr((seen, lline)))
    # Feature refusals follow the rule too: of two clashes, the first in text.
    for names in (("q", "p"), ("first_long", "s", "kk", "a2", "zeta", "m"), ("m1", "zz9", "b", "cc_c", "d", "e")):
        lib = "".join("%s := %d\n" % (n, k) for k, n in enumerate(names)) + 'fn(h()) { return 1 }\nexport(h)\n'
        entry = "".join("%s := %d\n" % (n, k) for k, n in enumerate(reversed(names))) + 'import("lib.f")\nprint(h())\n'
        refused("D clash reported at the first in text (%s)" % ",".join(names), {"lib.f": lib, "m.f": entry},
                "import-name-clash", "lib.f:1:1")


def held(path, exclusive=True):
    f = open(path, "a+")
    fcntl.flock(f, fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH)
    return f


def free(path):
    with open(path, "a+") as f:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.flock(f, fcntl.LOCK_UN)
            return True
        except BlockingIOError:
            return False


def section_e():
    """The package lock (Q2): housekeeping only; adversarial tests 1 and 2."""
    demo = 'fn(demo_v()) { return "demo" }\nexport(demo_v)\n'
    old = os.umask(0o022)
    # Adversarial 1: a missing package leaves a valid lock.
    root = tree({".nift/packages": None, ".nift/.packages-transaction.lock": "known lock bytes\n",
                 "m.f": 'print("before")\ntouch("marker.txt")\nimport("demo")\n'})
    lock = root / ".nift/.packages-transaction.lock"
    os.chmod(lock, 0o640)
    mode = stat.S_IMODE(lock.stat().st_mode)
    vm, rows = run(["m.f"], root)
    check(vm.returncode == 1 and vm.stdout == "" and not (root / "marker.txt").exists(),
          "E1 missing package: fails before the earlier statements", repr((vm.stdout, vm.stderr)))
    check(lock.read_bytes() == b"known lock bytes\n" and stat.S_IMODE(lock.stat().st_mode) == mode,
          "E1 lock contents and mode preserved", repr(lock.read_bytes()))
    check(free(lock), "E1 lock left unlocked")
    legacy, _ = run(["m.f"], root, "0")
    check("package is not installed: demo" in legacy.stderr, "E1 a later legacy resolution takes the lock", legacy.stderr)
    (root / "m.f").write_text('import("demo")\n')
    vm2, _ = run(["m.f"], root)
    check("package is not installed: demo" in vm2.stderr and free(lock) and lock.read_bytes() == b"known lock bytes\n",
          "E1 a later VM resolution takes the lock", vm2.stderr)
    shutil.rmtree(root)
    # No lock file: created empty (0644 under umask 022), the rest holds.
    root = tree({**package("demo", demo), "m.f": 'import("demo")\nprint(demo_v())\n'})
    lock = root / ".nift/.packages-transaction.lock"
    vm, _ = run(["m.f"], root)
    check(vm.stdout == "demo\n" and lock.is_file() and lock.read_bytes() == b"" and
          stat.S_IMODE(lock.stat().st_mode) == 0o644 and free(lock), "E1 missing lock file: created empty 0644, free",
          repr((vm.stdout, vm.stderr)))
    shutil.rmtree(root)
    # No `.nift/`: nothing is created.
    root = tree({"m.f": 'import("demo")\n'})
    vm, _ = run(["m.f"], root)
    check(not (root / ".nift").exists() and "package is not installed: demo" in vm.stderr,
          "E1 no .nift: none created", repr(sorted(p.name for p in root.iterdir())))
    shutil.rmtree(root)
    # Lock acquisition failures: package-management errors with their own id.
    for label, setup, text in (
            ("lock path is a directory", lambda r: (r / ".nift/.packages-transaction.lock").mkdir(),
             "cannot acquire package read lock"),
            ("lock file is a symlink", lambda r: os.symlink(r / "elsewhere", r / ".nift/.packages-transaction.lock"),
             "cannot acquire package read lock"),
            ("journal present", lambda r: (r / ".nift/package-transaction.json").write_text("{}"),
             "unfinished package transaction; run a package command to recover it")):
        root = tree({**package("demo", demo), "elsewhere": "x", "m.f": EARLY + 'import("demo")\n'})
        setup(root)
        vm, rows = run(["m.f"], root)
        check(vm.returncode == 1 and vm.stdout == "" and (": import: " + text) in vm.stderr and NOTE in vm.stderr and
              "not supported" not in vm.stderr and any(r.get("reason") == "import-error:package-lock" for r in rows),
              "E lock failure (" + label + "): package-management error, own id", repr((vm.stderr, rows)))
        legacy, _ = run(["m.f"], root, "0")
        check(text in legacy.stderr, "E lock failure (" + label + "): legacy's text", legacy.stderr)
        shutil.rmtree(root)
    # A held exclusive lock: the VM waits, then resolves (like legacy).
    root = tree({**package("demo", demo), ".nift/.packages-transaction.lock": "", "m.f": 'import("demo")\nprint(demo_v())\n'})
    lock = root / ".nift/.packages-transaction.lock"
    h = held(lock)
    proc = subprocess.Popen([NIFT, "m.f"], cwd=root, env=env_for(None), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True)
    time.sleep(1.0)
    waiting = proc.poll() is None
    h.close()
    out, err = proc.communicate(timeout=30)
    check(waiting and proc.returncode == 0 and out == "demo\n", "E held exclusive lock: the VM waits, then runs",
          repr((waiting, proc.returncode, out, err)))
    # Killed while waiting: the lock is released with the process.
    h = held(lock)
    proc = subprocess.Popen([NIFT, "m.f"], cwd=root, env=env_for(None), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.5)
    proc.send_signal(signal.SIGKILL)
    proc.wait(timeout=30)
    h.close()
    check(free(lock), "E killed while waiting: lock free afterwards")
    # The lock is released before the program runs (every path).
    (root / "m.f").write_text('import("demo")\ni := 0\nwhile (i < 200000000) { i += 1 }\nprint(demo_v())\n')
    proc = subprocess.Popen([NIFT, "m.f"], cwd=root, env=env_for(None), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True)
    time.sleep(0.6)
    running = proc.poll() is None
    released = free(lock)
    out, err = proc.communicate(timeout=120)
    check(running and released and out == "demo\n", "E lock released before the program runs",
          repr((running, released, out, err)))
    for label, src in (("program error", 'import("demo")\nimport("missing.f")\n'),
                       ("feature refusal", 'import("demo")\nfn(f()) { import("./x.f") }\n')):
        (root / "m.f").write_text(src)
        before = snapshot(root)
        run(["m.f"], root)
        check(free(lock) and snapshot(root) == before, "E after a " + label + ": lock free, nothing else written")
    shutil.rmtree(root)
    # Adversarial 2: concurrent readers (cold start, no lock file), VM and legacy.
    root = tree({**package("demo", demo), "m.f": 'import("demo")\nprint(demo_v())\n'})
    results = []

    def reader(mode, n):
        for _ in range(n):
            p = subprocess.run([NIFT, "m.f"], cwd=root, env=env_for(mode), capture_output=True, text=True, timeout=60)
            results.append((mode, p.returncode, p.stdout))
    threads = [threading.Thread(target=reader, args=(m, 8)) for m in (None, None, None, "0", "0", "0")]
    start = time.time()
    for t in threads:
        t.start()
    for t in threads:
        t.join(timeout=300)
    check(all(not t.is_alive() for t in threads) and len(results) == 48 and
          all(r[1] == 0 and r[2] == "demo\n" for r in results),
          "E2 concurrent VM and legacy readers: all resolve, no deadlock",
          repr([r for r in results if r[1] != 0][:3]))
    locks = [p for p in (root / ".nift").iterdir() if p.name.startswith(".packages-transaction")]
    check(len(locks) == 1 and locks[0].is_file() and locks[0].read_bytes() == b"", "E2 exactly one lock file, empty",
          repr(locks))
    check(time.time() - start < 300, "E2 within the time limit")
    shutil.rmtree(root)
    # Adversarial 2: a legacy writer (`nift update`, exclusive transaction lock) against VM readers.
    if shutil.which("git"):
        writer_test()
    else:
        print("SKIP E2 writer (git unavailable)")
    os.umask(old)


def git(cwd, *args):
    subprocess.run(["git", "-C", str(cwd), *args], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def writer_test():
    base = Path(tempfile.mkdtemp(dir=WORK))
    repo = base / "repo"
    (repo / "src").mkdir(parents=True)
    (repo / "manifest.json").write_text('{"name":"demo","version":"0.1.0","entry":"src/main.f"}\n')
    (repo / "src/main.f").write_text('value := "v0"\nexport(value)\n')
    git(repo, "init", "-q")
    git(repo, "config", "user.email", "a@b.c")
    git(repo, "config", "user.name", "test")
    git(repo, "add", ".")
    git(repo, "commit", "-qm", "v0")
    site = base / "site"
    site.mkdir()
    p = subprocess.run([NIFT, "add", "file://%s" % repo], cwd=site, env=env_for("0"), capture_output=True, text=True,
                       timeout=120)
    check(p.returncode == 0, "E2 writer: legacy `nift add` installs", p.stderr)
    (site / "m.f").write_text('import("demo")\nprint(value)\n')
    # A writer held at its exclusive lock: the VM reader waits until it is released.
    hold = base / "hold"
    hold.mkdir()
    (repo / "src/main.f").write_text('value := "v1"\nexport(value)\n')
    git(repo, "commit", "-qam", "v1")
    writer = subprocess.Popen([NIFT, "update", "demo"], cwd=site, env=env_for("0", {"NIFT_TEST_PACKAGE_TXN_HOLD": str(hold)}),
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.time() + 60
    while not (hold / "acquired").exists() and time.time() < deadline:
        time.sleep(0.02)
    reader = subprocess.Popen([NIFT, "m.f"], cwd=site, env=env_for(None), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True)
    time.sleep(1.0)
    waited = reader.poll() is None
    (hold / "release").write_text("")
    wout, werr = writer.communicate(timeout=120)
    rout, rerr = reader.communicate(timeout=120)
    check(waited and writer.returncode == 0 and reader.returncode == 0 and rout == "v1\n",
          "E2 VM reader waits for a held legacy writer, then sees its result",
          repr((waited, writer.returncode, werr, reader.returncode, rout, rerr)))
    # A writer loop against VM readers: every read sees a consistent state, nothing hangs.
    stop = [False]
    seen = []

    def readers():
        while not stop[0]:
            r = subprocess.run([NIFT, "m.f"], cwd=site, env=env_for(None), capture_output=True, text=True, timeout=60)
            seen.append((r.returncode, r.stdout, r.stderr[:200]))
    ts = [threading.Thread(target=readers) for _ in range(3)]
    for t in ts:
        t.start()
    versions = []
    for k in range(2, 8):
        (repo / "src/main.f").write_text('value := "v%d"\nexport(value)\n' % k)
        git(repo, "commit", "-qam", "v%d" % k)
        w = subprocess.run([NIFT, "update", "demo"], cwd=site, env=env_for("0"), capture_output=True, text=True,
                           timeout=120)
        versions.append(w.returncode)
    stop[0] = True
    for t in ts:
        t.join(timeout=120)
    good = {"v%d\n" % k for k in range(1, 8)}
    check(all(v == 0 for v in versions) and seen and all(r[0] == 0 and r[1] in good for r in seen) and
          all(not t.is_alive() for t in ts),
          "E2 writer loop: %d VM reads, each a consistent installed state, no hang" % len(seen),
          repr([r for r in seen if r[0] != 0 or r[1] not in good][:3]))
    final = subprocess.run([NIFT, "m.f"], cwd=site, env=env_for("0"), capture_output=True, text=True, timeout=60)
    check(final.stdout == "v7\n" and not (site / ".nift/package-transaction.json").exists(),
          "E2 package state valid afterwards (legacy reads the last install, no journal)", final.stdout + final.stderr)
    shutil.rmtree(base)


def section_f():
    """args/cmd in modules (Q4/Q5, C1)."""
    fn = 'fn(nargs()) { return args.size() }\nfn(ncmd()) { return cmd }\nfn(farg()) { return args[0] }\n' \
         'export(nargs)\nexport(ncmd)\nexport(farg)\n'
    a = ("one", "two w")
    parity("F module functions read the entry's args/cmd", {"lib.f": fn, "m.f": 'import("lib.f")\nprint(nargs())\n'
                                                            'print(ncmd())\nprint(farg())\n'}, args=a,
           expect="2\nm.f\none\n")
    parity("F through another module's function", {"lib.f": fn, "mid.f": 'import("lib.f")\nfn(via()) { return nargs() }\n'
                                                   'export(via)\n', "m.f": 'import("mid.f")\nprint(via())\n'}, args=a,
           expect="2\n")
    parity("F entry replacement before the import", {"lib.f": fn, "m.f": 'args := ["mine"]\nimport("lib.f")\n'
                                                     'print(nargs())\n'}, args=a, expect="1\n")
    parity("F function values and lambdas", {"lib.f": fn, "m.f": 'import("lib.f")\nf := nargs\nprint(f())\n'
                                             'l := () => nargs()\nprint(l())\n'}, args=a, expect="2\n2\n")
    parity("F a top-level lambda called from the entry", {"lib.f": 'g := () => args.size()\nexport(g)\n',
                                                          "m.f": 'import("lib.f")\nprint(g())\n'}, args=a, expect="2\n")
    # The private binding (C1 required tests).
    parity("F private args", {"lib.f": 'args := ["own"]\nfn(n()) { return args.size() }\nfn(f0()) { return args[0] }\n'
                              'print("top " + args[0])\nexport(n)\nexport(f0)\n',
                              "m.f": 'import("lib.f")\nprint(n())\nprint(f0())\nprint(args.size())\n'}, args=a,
           expect="top own\n1\nown\n2\n")
    parity("F two modules, each a private args", {
        "a.f": 'args := ["a1"]\nfn(av()) { return args.join(",") }\nexport(av)\n',
        "b.f": 'args := ["b1", "b2"]\nfn(bv()) { return args.join(",") }\nexport(bv)\n',
        "m.f": 'import("a.f")\nimport("b.f")\nprint(av())\nprint(bv())\nprint(args.join(","))\n'}, args=a,
           expect="a1\nb1,b2\none,two w\n")
    parity("F exported function reads its own module's args (from the entry and from another module)", {
        "p.f": 'args := ["p"]\nfn(pv()) { return args[0] }\nexport(pv)\n',
        "q.f": 'import("p.f")\nfn(qv()) { return "q:" + pv() }\nexport(qv)\nexport(pv)\n',
        "m.f": 'import("q.f")\nprint(pv())\nprint(qv())\nprint(args[0])\n'}, args=a, expect="p\nq:p\none\n")
    parity("F a private-args module beside one reading the entry's", {
        "p.f": 'args := ["p"]\nfn(pv()) { return args[0] }\nexport(pv)\n',
        "e.f": 'fn(ev()) { return args[0] }\nexport(ev)\n',
        "m.f": 'import("p.f")\nimport("e.f")\nprint(pv())\nprint(ev())\n'}, args=a, expect="p\none\n")
    parity("F nested: private module importing a module that reads the entry's", {
        "e.f": 'fn(ev()) { return args[0] }\nexport(ev)\n',
        "p.f": 'args := ["p"]\nimport("e.f")\nfn(pv()) { return args[0] + "/" + ev() }\nexport(pv)\nexport(ev)\n',
        "m.f": 'import("p.f")\nprint(pv())\n'}, args=a, expect="p/one\n")
    parity("F nested: module reading the entry's importing a private module", {
        "p.f": 'args := ["p"]\nfn(pv()) { return args[0] }\nexport(pv)\n',
        "e.f": 'import("p.f")\nfn(ev()) { return args[0] + "/" + pv() }\nexport(ev)\n',
        "m.f": 'import("e.f")\nprint(ev())\n'}, args=a, expect="one/p\n")
    parity("F private args reassigned at top level and in a function", {
        "p.f": 'args := ["p"]\nargs = ["q"]\nfn(put_r()) { args = ["r"]\n return args[0] }\nfn(read_a()) { return args[0] }\n'
               'export(put_r)\nexport(read_a)\n',
        "m.f": 'import("p.f")\nprint(read_a())\nprint(put_r())\nprint(read_a())\nprint(args[0])\n'}, args=a,
           expect="q\nr\nr\none\n")
    parity("F private cmd", {"p.f": 'cmd := "mod"\nfn(c()) { return cmd }\nexport(c)\n',
                             "m.f": 'import("p.f")\nprint(c())\nprint(cmd)\n'}, args=a, expect="mod\nm.f\n")
    parity("F private args beside the entry's own replacement", {
        "p.f": 'args := ["p"]\nfn(pv()) { return args[0] }\nexport(pv)\n',
        "m.f": 'args := ["e"]\nimport("p.f")\nprint(pv())\nprint(args[0])\n'}, args=a, expect="p\ne\n")
    parity("F a generated-name collision is avoided", {
        "p.f": 'args := ["p"]\nnote := "nift__args__m1"\nfn(pv()) { return args[0] + note }\nexport(pv)\n',
        "m.f": 'nift__args__m1 := "entry"\nimport("p.f")\nprint(pv())\nprint(nift__args__m1)\n'}, args=a,
           expect="pnift__args__m1\nentry\n")
    # Messages quote the original name, never the generated one.
    vm = parity("F messages quote the private binding as args", {
        "p.f": 'const args := [1]\nfn(bad()) { args.push(2)\n return 1 }\nexport(bad)\n',
        "m.f": 'import("p.f")\nprint(bad())\n'}, args=a)
    check("cannot mutate const array: args\n" in vm.stderr and "nift__" not in vm.stderr,
          "F messages say args, never the generated name", vm.stderr)
    # Refused before any effect.
    refused("F module top level reads the entry's args", {"lib.f": 'w := args.size()\nexport(w)\n',
                                                          "m.f": EARLY + 'import("lib.f")\nprint(w)\n'},
            "import-scope:invocation-init", "lib.f:1:6", args=a)
    vm = refused("F module init calls its own reading function", {
        "lib.f": 'fn(n()) { return args.size() }\ny := n()\nexport(y)\n', "m.f": EARLY + 'import("lib.f")\nprint(y)\n'},
        "import-scope:invocation-init", "lib.f:1:18", args=a)
    check("note: reached while" in vm.stderr and "lib.f:2:6" in vm.stderr and "not supported" not in vm.stderr,
          "F init-time read: program-error wording and the initialising mention", vm.stderr)
    refused("F another module's init reaches the reading function", {
        "lib.f": fn, "mid.f": 'import("lib.f")\nw := nargs()\nexport(w)\n', "m.f": 'import("mid.f")\nprint(w)\n'},
        "import-scope:invocation-init", "lib.f:1:22", args=a)
    refused("F reached through a value at init", {
        "lib.f": 'fn(n()) { return args.size() }\nh := n\nw := h()\nexport(w)\n', "m.f": 'import("lib.f")\nprint(w)\n'},
        "import-scope:invocation-init", "lib.f:1:18", args=a)
    refused("F reached two functions deep at init", {
        "lib.f": 'fn(n()) { return args.size() }\nfn(k()) { return n() }\nw := k()\nexport(w)\n',
        "m.f": 'import("lib.f")\nprint(w)\n'}, "import-scope:invocation-init", "lib.f:1:18", args=a)
    refused("F conservative: an untaken call at init (recorded difference)", {
        "lib.f": 'fn(n()) { return args.size() }\nif (false) { print(n()) }\nexport(n)\n',
        "m.f": 'import("lib.f")\nprint(n())\n'}, "import-scope:invocation-init", "lib.f:1:18", args=a)
    refused("F replacement after a module function may have read", {
        "lib.f": fn, "m.f": 'import("lib.f")\nprint(nargs())\nargs := ["mine"]\nprint(nargs())\n'}, "free-name", args=a)
    refused("F private args declared in a block", {"p.f": 'if (true) { args := [1] }\nfn(h()) { return 1 }\nexport(h)\n',
                                                   "m.f": 'import("p.f")\nprint(h())\n'},
            "import-scope:invocation-ambiguous", "p.f:1:13", args=a)
    refused("F private args read before its declaration", {"p.f": 'x := args.size()\nargs := [1]\nexport(x)\n',
                                                           "m.f": 'import("p.f")\nprint(x)\n'},
            "import-scope:invocation-ambiguous", "p.f:1:6", args=a)
    refused("F private args read by a function called before its declaration", {
        "p.f": 'fn(n()) { return args.size() }\nx := n()\nargs := [1]\nexport(x)\n', "m.f": 'import("p.f")\nprint(x)\n'},
        "import-scope:invocation-ambiguous", "p.f:1:18", args=a)
    # Exported into the entry: legacy's own error at the import (a program error).
    files = {"p.f": 'args := [1]\nexport(args)\n', "m.f": 'print("before")\nimport("p.f")\nprint(1)\n'}
    vm_root, lg_root = tree(files), tree(files)
    vm, rows = run(["m.f", *a], vm_root)
    legacy, _ = run(["m.f", *a], lg_root, "0")
    check(vm.returncode == 1 and vm.stdout == "" and
          error_line(vm.stderr).replace(str(vm_root), "R") == error_line(legacy.stderr).replace(str(lg_root), "R") and
          "export collides with existing binding: args" in vm.stderr and "not supported" not in vm.stderr and
          any(r.get("reason") == "import-scope:invocation-export" for r in rows),
          "F export of the private args into the entry: legacy's error, a program error",
          repr((vm.stderr, legacy.stderr, rows)))
    shutil.rmtree(vm_root)
    shutil.rmtree(lg_root)
    refused("F export of the private args into another module (not proved)",
            {"p.f": 'args := [1]\nexport(args)\n', "q.f": 'import("p.f")\nfn(h()) { return 1 }\nexport(h)\n',
             "m.f": 'import("q.f")\nprint(h())\n'}, "import-scope:invocation-ambiguous", "p.f:2:1", args=a)
    refused("F a local named args in a module function", {"p.f": 'fn(h()) { args := [1]\n return args[0] }\nexport(h)\n',
                                                          "m.f": 'import("p.f")\nprint(h())\n'}, "binding-name-reserved",
            args=a)
    refused("F a parameter named args in a module function", {"p.f": 'fn(h(args)) { return args }\nexport(h)\n',
                                                              "m.f": 'import("p.f")\nprint(h(1))\n'},
            "param-name-reserved", args=a)


def section_g():
    """Runtime errors in imported modules keep their semantics; prefixes per spelling."""
    for label, files in (("@import", {"dz.f": 'x := 0\ny := 5 / x\n', "m.f": 'print("b")\n@import("dz.f")\n'}),
                         ("@import then import", {"dz.f": 'x := 0\ny := 5 / x\n', "mdz.f": 'import("dz.f")\n',
                                                  "m.f": 'print("b")\n@import("mdz.f")\n'}),
                         ("package runtime error", {**package("demo", 'x := 1\ny := x / 0\n'),
                                                    "m.f": 'print("b")\nimport("demo")\n'})):
        root_v = tree(files)
        root_l = tree(files)
        vm, rows = run(["m.f"], root_v)
        legacy, _ = run(["m.f"], root_l, "0")
        check((vm.returncode, vm.stdout, vm.stderr.replace(str(root_v), "R")) ==
              (legacy.returncode, legacy.stdout, legacy.stderr.replace(str(root_l), "R")) and vm.stdout == "b\n",
              "G runtime error in an imported module (" + label + "): legacy's timing, text and prefixes",
              repr((vm.stderr, legacy.stderr)))
        shutil.rmtree(root_v)
        shutil.rmtree(root_l)


def section_l():
    """Legacy package behaviour unchanged (C2): its call paths keep their effects and texts."""
    demo = 'fn(demo_v()) { return "demo" }\nexport(demo_v)\n'
    root = tree({"m.f": 'print("b")\nimport("demo")\n'})
    legacy, _ = run(["m.f"], root, "0")
    check(legacy.stdout == "b\n" and "package is not installed: demo" in legacy.stderr and
          (root / ".nift/.packages-transaction.lock").is_file(),
          "L legacy failing import still creates .nift/ and the lock (unchanged)", repr((legacy.stdout, legacy.stderr)))
    shutil.rmtree(root)
    root = tree({**package("demo", demo), ".nift/package-transaction.json": "{}", "m.f": 'print("b")\nimport("demo")\n'})
    legacy, _ = run(["m.f"], root, "0")
    check(legacy.stdout == "b\n" and "unfinished package transaction; run a package command to recover it" in
          legacy.stderr, "L legacy journal error unchanged", legacy.stderr)
    shutil.rmtree(root)
    root = tree({**package("demo", demo), ".nift/.packages-transaction.lock": "", "m.f": 'import("demo")\nprint(demo_v())\n'})
    h = held(root / ".nift/.packages-transaction.lock")
    proc = subprocess.Popen([NIFT, "m.f"], cwd=root, env=env_for("0"), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True)
    time.sleep(1.0)
    waiting = proc.poll() is None
    h.close()
    out, _ = proc.communicate(timeout=60)
    check(waiting and out == "demo\n", "L legacy reader still waits for an exclusive writer", repr((waiting, out)))
    shutil.rmtree(root)
    for name in ("src/PackageTransaction.h", "src/PackageTransaction.cpp", "src/PackageMetadata.h", "src/PackageGraphLock.h"):
        text = (REPO / name).read_text()
        check('#include "rt/' not in text, "L " + name + " includes nothing from src/rt")


def section_r():
    """Review of 3cd39c9c: provenance, legacy's first-executed order, the early-return note, display names."""
    sib = 'fn(s()) { return "sib" }\nexport(s)\n'
    # Package -> absolute import -> relative import: no frozen provenance (legacy's error).
    root = tree({**package("demo", "X"), ".nift/packages/demo/src/inner.f": 'import("./sib.f")\nfn(o()) { return s() }\n'
                 'export(o)\n', ".nift/packages/demo/src/sib.f": sib, "m.f": EARLY + 'import("demo")\nprint(o())\n'})
    (root / ".nift/packages/demo/src/main.f").write_text('import("%s/.nift/packages/demo/src/inner.f")\nexport(o)\n' % root)
    lg = Path(tempfile.mkdtemp(dir=WORK))
    shutil.rmtree(lg)
    shutil.copytree(root, lg)
    (lg / ".nift/packages/demo/src/main.f").write_text('import("%s/.nift/packages/demo/src/inner.f")\nexport(o)\n' % lg)
    vm, rows = run(["m.f"], root)
    legacy, _ = run(["m.f"], lg, "0")
    check(vm.returncode == 1 and vm.stdout == "" and
          error_line(vm.stderr).replace(str(root), "R") == error_line(legacy.stderr).replace(str(lg), "R") and
          "package-relative import has no frozen package provenance" in vm.stderr and NOTE in vm.stderr,
          "R absolute import below a package, then relative: legacy's provenance error", repr((vm.stderr, legacy.stderr)))
    shutil.rmtree(root)
    shutil.rmtree(lg)
    # The other side of the boundary: provenance kept through relative imports (two levels, subdirectories).
    parity("R provenance kept: package -> relative -> relative", {
        **package("demo", 'import("./sub/a.f")\nexport(av)\n'),
        ".nift/packages/demo/src/sub/a.f": 'import("../b.f")\nfn(av()) { return "a+" + bv() }\nexport(av)\n',
        ".nift/packages/demo/src/b.f": 'import("./c/d.f")\nfn(bv()) { return "b+" + dv() }\nexport(bv)\n',
        ".nift/packages/demo/src/c/d.f": 'fn(dv()) { return "d" }\nexport(dv)\n',
        "m.f": 'import("demo")\nprint(av())\n'}, expect="a+b+d\n")
    # Kept provenance, then an absolute import (inside the package): that module has none (legacy's error).
    root = tree({**package("demo", 'import("./rel.f")\nexport(o)\n'),
                 ".nift/packages/demo/src/rel.f": "X", ".nift/packages/demo/src/abs.f":
                 'import("./sib.f")\nfn(o()) { return s() }\nexport(o)\n', ".nift/packages/demo/src/sib.f": sib,
                 "m.f": EARLY + 'import("demo")\nprint(o())\n'})
    (root / ".nift/packages/demo/src/rel.f").write_text('import("%s/.nift/packages/demo/src/abs.f")\nexport(o)\n' % root)
    vm, _ = run(["m.f"], root)
    legacy, _ = run(["m.f"], root, "0")
    check(vm.returncode == 1 and error_line(vm.stderr) == error_line(legacy.stderr) and
          "no frozen package provenance" in vm.stderr, "R provenance kept, then absolute, then relative: legacy's error",
          repr((vm.stderr, legacy.stderr)))
    shutil.rmtree(root)
    # An absolute import below a package whose module imports nothing relatively: fine (no false refusal).
    root = tree({**package("demo", "X"), ".nift/packages/demo/src/abs.f": 'fn(o()) { return "abs" }\nexport(o)\n',
                 "m.f": 'import("demo")\nprint(o())\n'})
    (root / ".nift/packages/demo/src/main.f").write_text('import("%s/.nift/packages/demo/src/abs.f")\nexport(o)\n' % root)
    vm, _ = run(["m.f"], root)
    legacy, _ = run(["m.f"], root, "0")
    check(vm.returncode == legacy.returncode == 0 and vm.stdout == legacy.stdout == "abs\n",
          "R absolute import below a package without relative imports runs", repr((vm.stderr, legacy.stderr)))
    shutil.rmtree(root)
    # The same with the absolute target outside the package (legacy's text, not "escapes").
    root = tree({**package("demo", "X"), "out/o.f": 'import("./sib.f")\nfn(o()) { return s() }\nexport(o)\n',
                 "out/sib.f": sib, "m.f": EARLY + 'import("demo")\nprint(o())\n'})
    (root / ".nift/packages/demo/src/main.f").write_text('import("%s/out/o.f")\nexport(o)\n' % root)
    vm, _ = run(["m.f"], root)
    legacy, _ = run(["m.f"], root, "0")
    check(error_line(vm.stderr) == error_line(legacy.stderr) and "no frozen package provenance" in vm.stderr,
          "R absolute import outside the package, then relative: legacy's text", repr((vm.stderr, legacy.stderr)))
    shutil.rmtree(root)
    # Legacy's first-executed order.
    same_error_as_legacy("R module that does not parse stops the walk",
                         {"bad.f": 'fn(f() {\n', "m.f": EARLY + 'import("bad.f")\nimport("missing.f")\n'},
                         reason="module-error:syntax")
    same_error_as_legacy("R module that does not parse (alone)", {"bad.f": 'fn(f() {\n', "m.f": EARLY + 'import("bad.f")\n'},
                         reason="module-error:syntax")
    same_error_as_legacy("R legacy-runtime-error region in a module stops the walk",
                         {"mod.f": 'f := (a,) => a\nimport("missing.f")\n', "m.f": EARLY + 'import("mod.f")\n'},
                         reason="module-error:syntax")
    # Several errors: the VM's own documented order (contract §4.6): the module graph first (depth first,
    # statement order, first problem stops it), then the linker's checks (here the args/cmd planner), then the
    # compiler. Each case reports a genuine, classified, positioned error before any effect.
    genuine("R graph error before the planner's invocation-init",
            {"a.f": 'n := args.size()\nfn(f()) { return n }\nexport(f)\n', "m.f": 'import("a.f")\nimport("missing.f")\n'},
            "import-error:missing", "R/m.f:2:1", args=("x",))
    genuine("R graph export-missing before the planner's invocation-init",
            {"b.f": 'n := args.size()\nexport(nope)\n', "m.f": 'import("b.f")\n'},
            "module-error:export-missing", "R/b.f:2:1", args=("x",))
    genuine("R invocation-init when the graph is clean",
            {"b.f": 'n := args.size()\nexport(n)\n', "m.f": 'import("b.f")\nprint(n)\n'},
            "import-scope:invocation-init", "R/b.f:1:6", args=("x",))
    genuine("R graph error before a compile-time error", {"m.f": 'x := 1\nx := 2\nimport("missing.f")\n'},
            "import-error:missing", "R/m.f:3:1")
    # The early-return note only for an unconditional top-level `return`.
    for label, files in (("R conditional return in the entry: no note",
                          {"m.f": 'x := 0\nif (x == 1) { return }\nprint("b")\nimport("missing.f")\n'}),
                         ("R conditional return in a module: no note",
                          {"mod.f": 'if (false) { return }\nimport("missing.f")\nfn(f()) { return 1 }\nexport(f)\n',
                           "m.f": 'import("mod.f")\n'})):
        vm, _ = same_error_as_legacy(label, files)
        check("legacy would stop there" not in vm.stderr, label + ": note absent", vm.stderr)
    # Determinism across these orders too.
    files = {"bad.f": 'fn(f() {\n', "a.f": 'n := args.size()\nexport(n)\n', "m.f": 'import("bad.f")\nimport("a.f")\n'}
    seen = set()
    for order in itertools.permutations(list(files)):
        root = Path(tempfile.mkdtemp(dir=WORK))
        for rel in order:
            (root / rel).write_text(files[rel])
        vm, _ = run(["m.f", "x"], root)
        seen.add(error_line(vm.stderr).replace(str(root), "R"))
        shutil.rmtree(root)
    check(len(seen) == 1 and "R/bad.f:1:1:" in next(iter(seen)), "R determinism: syntax error first", repr(seen))
    # Display names are whole identifiers (12 private-args modules).
    files = {"x%d.f" % k: 'args := [%d]\nfn(f%d()) { return args[0] }\nexport(f%d)\n' % (k, k, k) for k in range(1, 13)}
    files["x11.f"] = 'const args := [11]\nfn(f11()) { args.push(1)\n return 1 }\nexport(f11)\n'
    files["m.f"] = "".join('import("x%d.f")\n' % k for k in range(1, 13)) + "print(f1())\nprint(f12())\nprint(f11())\n"
    vm = parity("R 12 modules with private args", files)
    check("cannot mutate const array: args\n" in vm.stderr and "nift__" not in vm.stderr,
          "R display names at module index >= 10", vm.stderr)
    # classify / classify-all report program errors as program errors.
    root = tree({"m.f": EARLY + 'import("missing.f")\n'})
    for mode in ("classify", "classify-all"):
        p, rows = run(["m.f"], root, mode)
        check(any(r.get("outcome") == "program-error" and r.get("reason") == "import-error:missing" for r in rows),
              "R NIFT_VM=%s: outcome program-error" % mode, repr(rows))
        if mode == "classify-all":
            check('"detail":"program-error"' in json.dumps(rows).replace(" ", "") or
                  any("program-error" in json.dumps(r.get("reasons", "")) for r in rows),
                  "R classify-all inventory marks the program error", repr(rows))
    shutil.rmtree(root)


def section_s():
    """Errors in code that never runs do not matter; collisions name the binding that collides (F3)."""
    for label, entry in (
            ("S redeclaration in a function never called", 'fn(g()) { y := 1\n y := 2\n return y }\nimport("missing.f")\n'),
            ("S arity error in a function never called",
             'fn(g(a)) { return a }\nfn(h()) { return g() }\nimport("missing.f")\n'),
            ("S redeclaration in a branch not taken", 'x := 0\nif (x == 1) { y := 1\n y := 2 }\nimport("missing.f")\n')):
        same_error_as_legacy(label + ": the missing import, as legacy", {"m.f": entry}, reason="import-error:missing")
    genuine("S graph error reported before the compiler runs", {"m.f": 'x := 1\nx := 2\nimport("missing.f")\n'},
            "import-error:missing", "R/m.f:3:1")
    pa = 'args := [1,2]\nfn(g()) { return args.size() }\nexport(args)\nexport(g)\n'
    pa2 = 'args := [9]\nfn(g()) { return args.size() }\nexport(g)\nexport(args)\n'
    for label, files in (
            ("S colliding import before a missing import", {"pa2.f": pa2, "m.f": 'import("pa2.f")\nimport("missing.f")\n'}),
            ("S colliding import before a module with a missing import",
             {"pa.f": pa, "mm.f": 'import("missing.f")\n', "m.f": 'import("pa.f")\nimport("mm.f")\n'}),
            ("S colliding cmd", {"pc.f": 'cmd := "c"\nexport(cmd)\n', "m.f": 'print("b")\nimport("pc.f")\n'})):
        same_error_as_legacy(label, files, reason="import-scope:invocation-export", args=("a",))
    # The walk stops at the first problem: an earlier feature refusal is reported (owner policy).
    genuine("S feature refusal before invocation-export",
            {"nested.f": 'fn(q()) { import("ok.f") }\nfn(z()) { return 1 }\nexport(z)\n', "ok.f": 'fn(f()) { return 1 }\n'
             'export(f)\n', "pa.f": pa, "m.f": 'import("nested.f")\nimport("pa.f")\nprint(g())\n'},
            "import-nested", "R/nested.f:1:11", program_error=False, args=("a",))
    # F3: the collision names the binding that collides, in the module's export order.
    genuine("S c11 args exported before cmd names args",
            {"pz.f": 'cmd := "c"\nargs := [1]\nexport(args)\nexport(cmd)\n', "m.f": 'import("pz.f")\n'},
            "import-scope:invocation-export", "R/m.f:1:1", "export collides with existing binding: args", args=("a",))
    genuine("S c11b cmd exported before args names cmd",
            {"pz.f": 'cmd := "c"\nargs := [1]\nexport(cmd)\nexport(args)\n', "m.f": 'import("pz.f")\n'},
            "import-scope:invocation-export", "R/m.f:1:1", "export collides with existing binding: cmd", args=("a",))
    vm = genuine("S b08 an ordinary collision first is the import-collision refusal (x)",
                 {"px.f": 'x := 5\nargs := [1]\nexport(x)\nexport(args)\n', "m.f": 'x := 1\nimport("px.f")\n'},
                 "import-collision", "R/m.f:2:1", program_error=False, args=("a",))
    check("args" not in error_line(vm.stderr), "S b08 does not name args", vm.stderr)
    genuine("S b08b args first names args",
            {"px.f": 'x := 5\nargs := [1]\nexport(args)\nexport(x)\n', "m.f": 'x := 1\nimport("px.f")\n'},
            "import-scope:invocation-export", "R/m.f:2:1", "export collides with existing binding: args", args=("a",))


def main():
    global NIFT, WORK
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", default=str(REPO / "nift"))
    parser.add_argument("--only", default="")
    opts = parser.parse_args()
    NIFT = str(Path(opts.nift).resolve())
    WORK = Path(tempfile.mkdtemp(prefix="nift-packages-"))
    (WORK / "home").mkdir()
    try:
        for name, fn in (("a", section_a), ("b", section_b), ("c", section_c), ("d", section_d), ("e", section_e),
                         ("f", section_f), ("g", section_g),
                         ("l", section_l), ("r", section_r), ("s", section_s)):
            if not opts.only or name in opts.only:
                fn()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("packages: %d checks, %d failed" % (COUNT[0], len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
