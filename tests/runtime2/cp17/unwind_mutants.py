#!/usr/bin/env python3
"""CP17 combined-tree corrections: real compiled mutants for the unwind
release contract (owner decision, 2026-10-28), the left-open summary (F3),
teardown under persistent allocation failure (F2) and the CP17 x CP18
allocation-failure fixes.

Each mutant applies one exact textual change (which must match exactly once)
and requires the named witness to FAIL with the expected text in its output.
A control (no change) must PASS every witness (dead-harness control), and the
program witness run against a missing binary must fail (dead-binary control).

B22 (combined-b3-b5.md section 76). This harness used to copy the working tree
(about 576 MB with .build/libffi, rt and pic) per mutant and run `make` in the
copy with 3600 s timeouts. The copy left out .build/obj-config, so the first
make in it rewrote the object signature and rebuilt every object (298 for nift,
299 for the pic set) for each of the 32 mutants: hours of compiling, never a
deadlock, and more disk than the local machine has. Now each mutant is built
the sanctioned way (tests/runtime2/sanctioned_mutant.py): only the mutated unit
is compiled, with its production compile line, into .build/mutant-nift, and
linked with the production objects; the mutant object must differ from the
production object (the C75 check, which replaces the old mtime/sha provenance).

  unit witness     tests/rt/unwind_file_release_unit.cpp, linked with $(RT_UNIT_OBJECTS)
  oom witness      tests/rt/handle_close_oom_unit.cpp with $(RT_HANDLE_SOURCES), compiled as its
                   Makefile rule does (the control objects once, the mutated unit per mutant)
  vm witness       tests/runtime2/cp17/unwind_handle_release.py against the mutant nift
  persist:PROGRAM  tests/runtime2/cp17/handle_allocation_persistent.cpp, one program, linked with
                   $(C_ABI_PIC)

Each witness run has a cap (--cap, default 300 s). A timeout alone is never a
kill: a run that times out is a kill only when the expected witness text was
already printed (the restored defect is itself a hang after the CHECK line);
otherwise it is a harness failure. Progress (mutant, phase, elapsed seconds) is
printed as it goes.

Usage: unwind_mutants.py [--keep] [--only NAME] [--cap SECONDS]   (through make: test-runtime2-cp17-unwind-mutants)
"""
import argparse
import hashlib
import os
import shutil
import signal
import subprocess
import sys
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
    # B22: the while-loop mutants' sites moved to compiler_loops.cpp (a43472ed); the text is unchanged.
    ("no-block-handles", "src/rt/compiler_blocks.cpp", "        if (kinds(id) & kKindHandle) return true;\n", "", "vm",
     "FAIL c-block-completed-then-throw (legacy)"),
    ("no-if-clear", "src/rt/compiler.cpp", "    if (!ok) return false;\n    block_clear(body_regs);\n",
     "    if (!ok) return false;\n", "vm", "FAIL c-block-completed-then-throw (legacy)"),
    ("no-else-clear", "src/rt/compiler.cpp", "        if (other_ok) block_clear(else_regs);\n", "", "vm",
     "FAIL c-else-completed-then-throw (legacy)"),
    ("no-while-iteration-clear", "src/rt/compiler_loops.cpp",
     "        const std::size_t iteration_end = fb_->here();\n        block_clear(body_regs);\n",
     "        const std::size_t iteration_end = fb_->here();\n", "vm", "FAIL c-while-throw-first (legacy)"),
    ("continue-skips-clear", "src/rt/compiler_loops.cpp",
     "        for (std::size_t at : loops_.back().continues) fb_->patch(at, iteration_end);",
     "        for (std::size_t at : loops_.back().continues) fb_->patch(at, loops_.back().start);", "vm",
     "FAIL c-continue-throw-first (legacy)"),
    ("no-while-break-clear", "src/rt/compiler_loops.cpp",
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
    ("no-while-iteration-clear-structure", "src/rt/compiler_loops.cpp",
     "        const std::size_t iteration_end = fb_->here();\n        block_clear(body_regs);\n",
     "        const std::size_t iteration_end = fb_->here();\n", "vm", "FAIL s-while-iteration-clear (structure)"),
    ("continue-skips-clear-structure", "src/rt/compiler_loops.cpp",
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


sys.path.insert(0, str(ROOT / "tests" / "runtime2"))
import build_toolchain as toolchain  # noqa: E402
import sanctioned_mutant  # noqa: E402

OOM_TEST = "tests/rt/handle_close_oom_unit.cpp"
WITNESS_UNIT = "tests/rt/unwind_file_release_unit.cpp"
PERSIST_TEST = "tests/runtime2/cp17/handle_allocation_persistent.cpp"


def text(x):
    return x.decode(errors="replace") if isinstance(x, bytes) else (x or "")


def execute(cmd, cap, env=None):
    """(returncode, output, timed_out); a timeout returns the output so far.

    Review of C83-C99, L-4 (combined-b3-b5.md section 90): the witness runs in its own session, so its
    process group is the one this harness started (its pgid is the child's pid). On the cap the whole
    group is killed, so a python witness's nift grandchildren cannot outlive it. Only that group is
    signalled; nothing is looked up by name."""
    p = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         start_new_session=True)
    try:
        out, err = p.communicate(timeout=cap)
        return p.returncode, out + err, False
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        out, err = p.communicate()
        return 124, text(out) + text(err), True


class Oom:
    """The handle-close-oom unit, built as its Makefile rule builds it: $(CXX) $(CPPFLAGS) -Itests/rt
    $(CXXFLAGS) TEST $(RT_HANDLE_SOURCES). The control objects are compiled once; a mutant
    recompiles only its unit, which must differ from the control object."""

    def __init__(self, work):
        self.dir = work / "oom-control"
        self.sources = toolchain.words("NIFT_TC_RT_HANDLE_SOURCES")
        self.objects = {}
        for src in self.sources:
            obj = self.dir / (src.replace("/", "_") + ".o")
            obj.parent.mkdir(parents=True, exist_ok=True)
            toolchain.run(toolchain.test_compile("-c", src, "-o", str(obj)), "oom control compile " + src)
            self.objects[src] = obj

    def link(self, objects, binary):
        toolchain.run(toolchain.test_compile(OOM_TEST, *map(str, objects), "-o", str(binary)), "oom link")
        return binary

    def control(self):
        return self.link(self.objects.values(), self.dir / "unit")

    def mutant(self, folder, path, old, new):
        if path not in self.objects:
            raise SystemExit(f"{path} is not in RT_HANDLE_SOURCES")
        folder.mkdir(parents=True, exist_ok=True)
        copy = folder / Path(path).name
        source = (ROOT / path).read_text()
        if source.count(old) != 1:
            raise SystemExit(f"the mutation site matches {source.count(old)} times in {path} (a stale mutant)")
        copy.write_text(source.replace(old, new, 1))
        obj = folder / (Path(path).name + ".o")
        toolchain.run(toolchain.test_compile("-iquote", str(Path(path).parent), "-c", str(copy), "-o", str(obj)),
                      "oom mutant compile " + path)
        if obj.read_bytes() == self.objects[path].read_bytes():
            raise SystemExit(f"the mutant object of {path} is identical to the control's (a dead mutant)")
        return self.link([obj if s == path else o for s, o in self.objects.items()], folder / "unit")


def build(kind, folder, edit, oom):
    """The witness binary for `kind`, mutated by `edit` (None: the control)."""
    if kind == "oom":
        return oom.mutant(folder, *edit) if edit else oom.control()
    if edit is None:
        return ROOT / {"unit": UNIT, "vm": "nift"}.get(kind, PERSIST)
    if kind == "unit":
        return sanctioned_mutant.build(folder, edits=[edit], objects_env="NIFT_TC_RT_UNIT_OBJECTS",
                                       unit=WITNESS_UNIT, quiet=True)
    if kind == "vm":
        return sanctioned_mutant.build(folder, edits=[edit], quiet=True)
    return sanctioned_mutant.build(folder, edits=[edit], config="pic", unit=PERSIST_TEST, quiet=True)


def witness(kind, binary, cap):
    if kind in ("unit", "oom"):
        return execute([str(binary)], cap)
    if kind.startswith("persist"):
        env = dict(os.environ)
        if ":" in kind:
            env["ONLY_PROG"] = kind.split(":", 1)[1]
        return execute([str(binary)], cap, env)
    return execute([sys.executable, "-B", "tests/runtime2/cp17/unwind_handle_release.py", "--nift", str(binary)], cap)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--only")
    ap.add_argument("--cap", type=int, default=300, help="seconds per witness run")
    args = ap.parse_args()
    os.chdir(ROOT)
    work = ROOT / ".build" / "mutant-nift" / "cp17-unwind"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    began = time.time()
    failures = 0

    def progress(message):
        print(f"[{time.time() - began:7.1f}s] {message}", flush=True)

    try:
        dead = subprocess.run([sys.executable, "-B", "tests/runtime2/cp17/unwind_handle_release.py", "--nift",
                               str(work / "missing-nift")], cwd=ROOT, capture_output=True, text=True, timeout=60)
        if dead.returncode == 0:
            print("FAIL dead-binary control: the witness passed without a binary")
            return 1
        progress("ok   dead-binary control: the witness fails without a binary")
        selected = [m for m in MUTANTS if not args.only or m[0] == args.only]
        if not selected:
            print(f"FAIL no mutant named {args.only}")
            return 1
        oom = Oom(work) if any(m[4] == "oom" for m in selected) else None
        for kind in sorted({m[4] for m in selected}):
            t = time.time()
            rc, out, timed_out = witness(kind, build(kind, work / "control", None, oom), args.cap)
            if rc != 0:
                print(f"FAIL control ({kind}): the unmutated witness does not pass: rc={rc}"
                      f"{' (timed out at the cap)' if timed_out else ''}\n{out[-1500:]}")
                return 1
            progress(f"ok   control ({kind}): passes in {time.time() - t:.1f}s")
        for number, (name, path, old, new, kind, expected) in enumerate(selected, 1):
            folder = work / name
            t = time.time()
            try:
                binary = build(kind, folder, (path, old, new), oom)
            except (SystemExit, RuntimeError) as e:
                print(f"FAIL {name}: mutant did not build (a build failure is not a kill): {e}")
                failures += 1
                shutil.rmtree(folder, ignore_errors=True)
                continue
            built = time.time() - t
            rc, out, timed_out = witness(kind, binary, args.cap)
            ran = time.time() - t - built
            where = f"{number}/{len(selected)}, build {built:.1f}s, witness {ran:.1f}s"
            if rc == 0:
                print(f"FAIL {name}: survived ({kind} witness passed; {where})")
                failures += 1
            elif expected not in out:
                print(f"FAIL {name}: {'timed out at the cap' if timed_out else 'killed'}, but not by the intended "
                      f"witness ({expected!r} absent; {where})\n{out[-1500:]}")
                failures += 1
            else:
                how = "witness text, then the cap" if timed_out else f"rc={rc}"
                progress(f"ok   {name}: killed by {kind} witness ({expected}; {how}; {where})")
            if not args.keep:
                shutil.rmtree(folder, ignore_errors=True)
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
    total = len([m for m in MUTANTS if not args.only or m[0] == args.only])
    print(f"{'PASS' if not failures else 'FAIL'} cp17 unwind release mutants: {total} mutant(s), {failures} failure(s) "
          f"in {time.time() - began:.0f}s")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
