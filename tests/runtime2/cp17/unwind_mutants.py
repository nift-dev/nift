#!/usr/bin/env python3
"""CP17 x CP13B decision B: real compiled mutants of the unwound-frame release.

Each mutant applies one exact textual change (which must match exactly once)
to a private copy of the tree, rebuilds the named artefact there and requires
the named witness to FAIL with the expected case named in its output. A
control copy with no change must PASS the same witnesses (dead-harness
control), and a witness run against a missing binary must fail (dead-binary
control). The copy keeps the source tree's build outputs, so only the mutated
translation unit and its links are rebuilt.

  unit witness   tests/rt/unwind_file_release_unit.cpp (make RT unit)
  vm witness     tests/runtime2/cp17/unwind_handle_release.py (make nift)

Usage: unwind_mutants.py [--keep] [--only NAME]
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UNIT = ".build/rt/unwind-file-release"
FILE_NATIVES = "src/rt/natives/host_file_handles.cpp"
GUARD = ("if (!value.boxed_handle() || value.refcount() != 1 || value.handle_kind() != HandleKind::File) "
         "return false;")
OWNER = "if (table.owner_of(value, HandleKind::File, slot_owner) != HandleStatus::Ok || slot_owner != owner) return false;"
MUTANTS = [
    # (name, file, old, new, kind, expected failing witness text)
    ("ignore-surviving-alias", FILE_NATIVES, GUARD,
     GUARD.replace(" || value.refcount() != 1", ""), "unit", "unwind_file_release_unit.cpp:"),
    ("ignore-owner", FILE_NATIVES, OWNER,
     "if (table.owner_of(value, HandleKind::File, slot_owner) != HandleStatus::Ok) return false;", "unit",
     "unwind_file_release_unit.cpp:"),
    ("not-discarded", FILE_NATIVES, "    file->open = false;\n    file->dirty = false;\n    file->mode.clear();",
     "    file->dirty = false;\n    file->mode.clear();", "unit", "unwind_file_release_unit.cpp:"),
    ("no-release-on-unwind", "src/rt/vm_exception.cpp",
     "            if (R[k].is(Kind::Handle)) abandon_unwound_handle(R[k]);\n", "", "vm", "FAIL reproducer (same)"),
    ("release-on-return", "src/rt/vm.cpp",
     "    for (std::uint16_t k = 1; k < proto->nregs; ++k) if (R[k].is_heap()) R[k] = Value();",
     "    for (std::uint16_t k = 1; k < proto->nregs; ++k) if (R[k].is_heap()) { if (R[k].is(Kind::Handle)) "
     "abandon_unwound_handle(R[k]); R[k] = Value(); }", "vm", "FAIL normal-return-leak (both-reported)"),
    ("release-handler-frame", "src/rt/vm_exception.cpp",
     "    for (std::uint16_t k = handler.reg_top; k < proto->nregs; ++k) R[k] = Value();\n",
     "    for (std::uint16_t k = handler.reg_top; k < proto->nregs; ++k) { if (R[k].is(Kind::Handle)) "
     "abandon_unwound_handle(R[k]); R[k] = Value(); }\n", "vm", "FAIL try-body-local (both-reported)"),
]


def run(cmd, cwd, timeout):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def copy_tree(dest):
    # Working tree plus build outputs, without .git (the copy is never committed).
    def ignore(directory, names):
        return [".git"] if Path(directory) == ROOT else []
    shutil.copytree(ROOT, dest, symlinks=True, ignore=ignore, copy_function=shutil.copy2)


def witness(tree, kind):
    if kind == "unit":
        build = run(["make", UNIT], tree, 3600)
        if build.returncode != 0:
            return None, build.stdout[-2000:] + build.stderr[-2000:]
        result = run([str(tree / UNIT)], tree, 300)
    else:
        build = run(["make", "-j2", "nift"], tree, 3600)
        if build.returncode != 0:
            return None, build.stdout[-2000:] + build.stderr[-2000:]
        result = run([sys.executable, "tests/runtime2/cp17/unwind_handle_release.py", "--nift", str(tree / "nift")],
                     tree, 900)
    return result.returncode, result.stdout + result.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--only")
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="cp17-unwind-mutants-"))
    failures = 0
    try:
        # Dead-binary control: the program witness must fail without a binary.
        dead = run([sys.executable, str(ROOT / "tests/runtime2/cp17/unwind_handle_release.py"), "--nift",
                    str(work / "missing-nift")], ROOT, 60)
        if dead.returncode == 0:
            print("FAIL dead-binary control: the witness passed without a binary")
            return 1
        print("ok   dead-binary control: the witness fails without a binary")
        control = work / "control"
        copy_tree(control)
        for kind in ("unit", "vm"):
            rc, out = witness(control, kind)
            if rc != 0:
                print(f"FAIL control ({kind}): unmutated tree does not pass: rc={rc}\n{out[-1500:]}")
                return 1
            print(f"ok   control ({kind}): unmutated tree passes")
        shutil.rmtree(control)
        for name, path, old, new, kind, expected in MUTANTS:
            if args.only and name != args.only:
                continue
            tree = work / name
            copy_tree(tree)
            source = (tree / path).read_text()
            if source.count(old) != 1:
                print(f"FAIL {name}: mutation site matches {source.count(old)} times in {path}")
                failures += 1
                shutil.rmtree(tree)
                continue
            (tree / path).write_text(source.replace(old, new, 1))
            rc, out = witness(tree, kind)
            if rc is None:
                print(f"FAIL {name}: mutant did not build (a build failure is not a kill)\n{out}")
                failures += 1
            elif rc == 0:
                print(f"FAIL {name}: survived ({kind} witness passed)")
                failures += 1
            elif expected not in out:
                print(f"FAIL {name}: killed, but not by the intended witness ({expected!r} absent)\n{out[-1500:]}")
                failures += 1
            else:
                print(f"ok   {name}: killed by {kind} witness ({expected})")
            if not args.keep:
                shutil.rmtree(tree)
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
    total = len([m for m in MUTANTS if not args.only or m[0] == args.only])
    print(f"{'PASS' if not failures else 'FAIL'} cp17 unwind release mutants: {total} mutant(s), {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
