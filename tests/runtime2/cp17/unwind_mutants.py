#!/usr/bin/env python3
"""CP17 combined-tree corrections: real compiled mutants for the unwind
release contract (owner decision, 2026-10-28), the left-open summary (F3),
teardown under persistent allocation failure (F2) and the CP17 x CP18
allocation-failure fixes.

Each mutant applies one exact textual change (which must match exactly once)
to a private copy of the tree, rebuilds the named artefact there and requires
the named witness to FAIL with the expected text in its output. A control copy
with no change must PASS every witness (dead-harness control), and the program
witness run against a missing binary must fail (dead-binary control). The copy
keeps the source tree's build outputs, so only the mutated translation unit
and its links are rebuilt.

Provenance (independent review, mutation integrity): the witness artefact,
and for a .cpp in an object build that file's object, must be rebuilt after
the mutation AND differ in content (sha256) from the control copy's; a
mutation that compiles to the control's bytes is reported, never counted.

  unit witness     tests/rt/unwind_file_release_unit.cpp (make RT unit)
  oom witness      tests/rt/handle_close_oom.cpp
  vm witness       tests/runtime2/cp17/unwind_handle_release.py (make nift)
  persist:PROGRAM  tests/runtime2/cp17/handle_allocation_persistent.cpp, one program

Usage: unwind_mutants.py [--keep] [--only NAME]
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UNIT = ".build/rt/unwind-file-release"
OOM_UNIT = ".build/rt/handle-close-oom"
PERSIST = ".build/cp17/handle-allocation-persistent"
FILE_NATIVES = "src/rt/natives/host_file_handles.cpp"
GUARD = ("if (!value.boxed_handle() || value.refcount() != 1 || value.handle_kind() != HandleKind::File) "
         "return false;")
OWNER = "if (table.owner_of(value, HandleKind::File, slot_owner) != HandleStatus::Ok || slot_owner != owner) return false;"
# History: decision B's six mutants were retired with B's removal
# (2026-10-27) and the statement rollback's eight with the rollback
# (2026-10-28: the owner replaced both by the unwind release contract after
# the independent review's F1). B's release function is back in the rework, so
# its ownership mutants are active again; release-handler-frame is inverted
# (no-try-body-release): the try body's registers are now released.
MUTANTS = [
    # (name, file, old, new, kind, expected failing witness text)
    # natives::abandon_file_on_unwind: exclusive ownership and silent discard.
    ("ignore-surviving-alias", FILE_NATIVES, GUARD,
     GUARD.replace(" || value.refcount() != 1", ""), "unit", "unwind_file_release_unit.cpp:"),
    ("ignore-owner", FILE_NATIVES, OWNER,
     "if (table.owner_of(value, HandleKind::File, slot_owner) != HandleStatus::Ok) return false;", "unit",
     "unwind_file_release_unit.cpp:"),
    ("not-discarded", FILE_NATIVES, "    file->open = false;\n    file->dirty = false;\n    file->mode.clear();",
     "    file->dirty = false;\n    file->mode.clear();", "unit", "unwind_file_release_unit.cpp:"),
    # Vm::unwind: what the Error unwound.
    ("no-release-on-unwind", "src/rt/vm_exception.cpp",
     "            if (R[k].is(Kind::Handle)) abandon_unwound_handle(R[k]);\n", "", "vm", "FAIL reproducer (legacy)"),
    ("no-try-body-release", "src/rt/vm_exception.cpp",
     "        if (R[k].is(Kind::Handle)) abandon_unwound_handle(R[k]);  // CP17 unwind release\n", "", "vm",
     "FAIL try-body-local (correction)"),
    ("release-on-return", "src/rt/vm.cpp",
     "    for (std::uint16_t k = 1; k < proto->nregs; ++k) if (R[k].is_heap()) R[k] = Value();",
     "    for (std::uint16_t k = 1; k < proto->nregs; ++k) if (R[k].is_heap()) { if (R[k].is(Kind::Handle)) "
     "abandon_unwound_handle(R[k]); R[k] = Value(); }", "vm", "FAIL normal-return-leak (legacy)"),
    # Compiler: a block left normally clears its registers (completed code is reported).
    ("no-block-handles", "src/rt/compiler_blocks.cpp", "        if (kinds(id) & kKindHandle) return true;\n", "", "vm",
     "FAIL c-block-completed-then-throw (legacy)"),
    ("no-if-clear", "src/rt/compiler.cpp", "    if (!ok) return false;\n    block_clear(body_regs);\n",
     "    if (!ok) return false;\n", "vm", "FAIL c-block-completed-then-throw (legacy)"),
    ("no-else-clear", "src/rt/compiler.cpp", "        if (other_ok) block_clear(else_regs);\n", "", "vm",
     "FAIL c-else-completed-then-throw (legacy)"),
    ("no-while-iteration-clear", "src/rt/compiler.cpp",
     "        const std::size_t iteration_end = fb_->here();\n        block_clear(body_regs);\n",
     "        const std::size_t iteration_end = fb_->here();\n", "vm", "FAIL c-while-throw-first (legacy)"),
    ("continue-skips-clear", "src/rt/compiler.cpp",
     "        for (std::size_t at : loops_.back().continues) fb_->patch(at, iteration_end);",
     "        for (std::size_t at : loops_.back().continues) fb_->patch(at, loops_.back().start);", "vm",
     "FAIL c-continue-throw-first (legacy)"),
    ("no-while-break-clear", "src/rt/compiler.cpp",
     "        for (std::size_t at : loops_.back().breaks) fb_->patch(at, fb_->here());\n        block_clear(body_regs);\n",
     "        for (std::size_t at : loops_.back().breaks) fb_->patch(at, fb_->here());\n", "vm",
     "FAIL c-break-then-throw (legacy)"),
    ("no-for-iteration-clear", "src/rt/compiler_loops.cpp",
     "    block_clear(state.body_regs);  // CP17 unwind release: the end of an iteration (also `continue`)\n", "", "vm",
     "FAIL c-for-throw-first (legacy)"),
    ("no-for-break-clear", "src/rt/compiler_loops.cpp",
     "    block_clear(state.body_regs);  // `break` leaves the body without its iteration end\n", "", "vm",
     "FAIL c-for-break-then-throw (legacy)"),
    ("no-try-body-clear", "src/rt/compiler_try.cpp",
     "    block_clear(body_regs);  // CP17 unwind release: the try body completed normally\n", "", "vm",
     "FAIL c-completed-inner-try-body (legacy)"),
    ("no-catch-clear", "src/rt/compiler_try.cpp", "    block_clear(catch_regs);\n", "", "vm",
     "FAIL c-completed-catch-body (legacy)"),
    # Independent review R1: block-clearing mutants the witnesses above missed.
    ("no-merge-high", "src/rt/compiler_blocks.cpp", "    fb_->merge_high(regs.outer_high);\n", "", "vm",
     "FAIL c-break-out-of-inner-try (legacy)"),
    ("if-clear-only-without-else", "src/rt/compiler.cpp",
     "    if (!ok) return false;\n    block_clear(body_regs);\n    if (node.c == kNoNode) {\n",
     "    if (!ok) return false;\n    if (node.c == kNoNode) {\n        block_clear(body_regs);\n", "vm",
     "FAIL c-then-completed-with-else (legacy)"),
    # Independent review R2: the two while-clear mutants again, now required to
    # be killed by the pad-free structural witnesses (register-placement
    # independent), not only by the padded behavioural ones.
    ("no-while-iteration-clear-structure", "src/rt/compiler.cpp",
     "        const std::size_t iteration_end = fb_->here();\n        block_clear(body_regs);\n",
     "        const std::size_t iteration_end = fb_->here();\n", "vm", "FAIL s-while-iteration-clear (structure)"),
    ("continue-skips-clear-structure", "src/rt/compiler.cpp",
     "        for (std::size_t at : loops_.back().continues) fb_->patch(at, iteration_end);",
     "        for (std::size_t at : loops_.back().continues) fb_->patch(at, loops_.back().start);", "vm",
     "FAIL s-while-continue-reaches-clear (structure)"),
    # F3: the left-open summary (count, and the most recently created file named).
    ("no-left-open-count", "src/rt/vm_handles.cpp", "    if (count < 2) return;\n", "    return;\n", "vm",
     "FAIL c-two-completed-names (legacy)"),
    ("left-open-names-first", "src/rt/vm_handles.cpp",
     "        if (newest == nullptr || f.sequence > newest->sequence) newest = &f;",
     "        if (newest == nullptr) newest = &f;", "vm", "FAIL c-two-completed-names (legacy)"),
    # F2: teardown under persistent allocation failure (never std::terminate).
    ("table-dtor-throws", "src/rt/handles/table_teardown.cpp",
     "        shutdown();\n        return;\n    } catch (...) {",
     "        shutdown();\n        return;\n    } catch (int) {", "persist:plain-no-handles",
     "CRASH plain-no-handles"),
    ("hostcall-dtor-throws", "src/rt/host/host_call.cpp",
     "        emit_v2(\"error\", e.code);\n    } catch (...) {",
     "        emit_v2(\"error\", e.code);\n    } catch (const std::logic_error&) {", "persist:environment",
     "CRASH environment"),
    # CP17 x CP18: close_context under allocation failure.
    ("close-mark-in-walk", "src/rt/handles/context_tree.cpp",
     "    const auto self = contexts_.find(id);\n    if (self == contexts_.end()) return;\n",
     "    const auto self = contexts_.find(id);\n    if (self == contexts_.end()) return;\n"
     "    const_cast<ExecContext&>(self->second).state = ContextState::Closing;\n", "oom",
     "HANG section=close"),
    ("close-no-restore", "src/rt/handles/table_contexts.cpp", "        bool armed = true;", "        bool armed = false;",
     "oom", "HANG section=close"),
    ("close-restore-to-open", "src/rt/handles/table_contexts.cpp", "c->state = c->before_close;",
     "c->state = ContextState::Open;", "oom", "CHECK failed: !t.open(c) && t.table.cancel_context(c)"),
    # CP17 x CP18: cleanup bookkeeping, create and ticket settlement under allocation failure.
    ("cleanup-no-recovery", "src/rt/handles/table.cpp", "        abandon_cleanup_locked(t.record, t.owner);\n", "",
     "oom", "HANG section=drop"),
    ("record-not-reserved", "src/rt/handles/table.cpp",
     "    if (status == HandleStatus::Ok) records_.reserve_one();\n", "", "oom",
     "CHECK failed: t.table.stats().live == before.live"),
    ("settle-no-recovery", "src/rt/handles/table_settle.cpp", "        lose_record_locked(id, owner);\n        throw;",
     "        throw;", "oom", "CHECK failed: table.stats().pending_obligations == 0"),
    ("settle-report-first", "src/rt/handles/table_settle.cpp",
     "    ++settled_;\n    release_record_locked(id, owner);\n    if (failed) {",
     "    ++settled_;\n    if (failed) failures_of(owner).push_back(failure);\n    release_record_locked(id, owner);\n"
     "    if (false) {", "oom", "CHECK failed: table.stats().pending_obligations == 0"),
    ("release-allocates", "src/rt/handles/slot_store.cpp",
     "        if (free_.capacity() < slots_.size() + 1)\n", "        if (false)\n", "oom",
     "CHECK failed: !threw && !injected"),
]


def run(cmd, cwd, timeout):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def copy_tree(dest):
    # Working tree plus build outputs, without .git (the copy is never committed).
    # Only the build outputs the witnesses reuse (libffi, rt units); the
    # sanitizer and other suites' trees would multiply disk use per mutant.
    def ignore(directory, names):
        if Path(directory) == ROOT:
            return [".git"]
        if Path(directory) == ROOT / ".build":
            return [n for n in names if n not in ("libffi", "rt", "pic")]
        return []
    shutil.copytree(ROOT, dest, symlinks=True, ignore=ignore, copy_function=shutil.copy2)


def artifact_of(kind):
    if kind == "oom":
        return OOM_UNIT
    if kind == "unit":
        return UNIT
    return PERSIST if kind.startswith("persist") else "nift"


def object_of(kind, path):
    """The object the mutated .cpp compiles to in the witness's build, or None."""
    if not path.endswith(".cpp"):
        return None
    if kind == "vm":
        return path[:-4] + ".o"
    if kind.startswith("persist"):
        return ".build/pic/" + path[:-4] + ".o"
    return None  # unit/oom binaries compile the sources directly


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None


def provenance(tree, kind, path, start, control):
    """Proof that the mutant's own production code was compiled into the
    witness: rebuilt after the mutation (mtime) and different from the
    control copy's bytes (sha256). Returns the problems."""
    problems = []
    for rel in filter(None, (artifact_of(kind), object_of(kind, path))):
        f = tree / rel
        if not f.exists() or f.stat().st_mtime < start:
            problems.append(f"{rel} not rebuilt after the mutation")
        elif control.get(rel) is not None and sha(f) == control[rel]:
            problems.append(f"{rel} identical to the control's (no compiled mutation)")
    return problems


def witness(tree, kind):
    if kind in ("unit", "oom"):
        target = UNIT if kind == "unit" else OOM_UNIT
        build = run(["make", target], tree, 3600)
        if build.returncode != 0:
            return None, build.stdout[-2000:] + build.stderr[-2000:]
        try:
            # A killed mutant may hang after its first failed CHECK (the defect
            # it restores is a hang); the CHECK line is the witness, the
            # timeout only bounds the run and never counts as a kill alone.
            result = run([str(tree / target)], tree, 120)
        except subprocess.TimeoutExpired as e:
            def text(x):
                return x.decode(errors="replace") if isinstance(x, bytes) else (x or "")
            return 124, text(e.stdout) + text(e.stderr)
    elif kind.startswith("persist"):
        build = run(["make", "-j2", PERSIST], tree, 3600)
        if build.returncode != 0:
            return None, build.stdout[-2000:] + build.stderr[-2000:]
        env = dict(os.environ)
        if ":" in kind:
            env["ONLY_PROG"] = kind.split(":", 1)[1]
        result = subprocess.run([str(tree / PERSIST)], cwd=tree, env=env, capture_output=True, text=True,
                                timeout=1800)
    else:
        build = run(["make", "-j2", "nift"], tree, 3600)
        if build.returncode != 0:
            return None, build.stdout[-2000:] + build.stderr[-2000:]
        result = run([sys.executable, "tests/runtime2/cp17/unwind_handle_release.py", "--nift", str(tree / "nift")],
                     tree, 1800)
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
        kinds = sorted({m[4] for m in MUTANTS if not args.only or m[0] == args.only})
        for kind in kinds:
            rc, out = witness(control, kind)
            if rc != 0:
                print(f"FAIL control ({kind}): unmutated tree does not pass: rc={rc}\n{out[-1500:]}")
                return 1
            print(f"ok   control ({kind}): unmutated tree passes")
        hashes = {}
        for _, path, _, _, kind, _ in MUTANTS:
            for rel in filter(None, (artifact_of(kind), object_of(kind, path))):
                hashes[rel] = sha(control / rel)
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
            start = time.time()
            rc, out = witness(tree, kind)
            stale = provenance(tree, kind, path, start, hashes) if rc is not None else []
            if stale:
                print(f"FAIL {name}: provenance: {'; '.join(stale)}")
                failures += 1
            elif rc is None:
                print(f"FAIL {name}: mutant did not build (a build failure is not a kill)\n{out}")
                failures += 1
            elif rc == 0:
                print(f"FAIL {name}: survived ({kind} witness passed)")
                failures += 1
            elif expected not in out:
                print(f"FAIL {name}: killed, but not by the intended witness ({expected!r} absent)\n{out[-1500:]}")
                failures += 1
            else:
                print(f"ok   {name}: killed by {kind} witness ({expected}); rebuilt from the mutated {path}")
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
