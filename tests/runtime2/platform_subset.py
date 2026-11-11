#!/usr/bin/env python3
"""Runtime 2.0 platform subsets for the first cross-platform certification (D-077 #4, owner 2026-11-12 04:25).

The exact Linux / macOS / Windows VM test subset, as data: every entry names its command and, for each platform
that excludes it, the concrete reason. No platform is skipped as a whole, and an entry never passes on a platform
that excludes it: it is reported as EXCLUDED with its reason. docs/runtime-2/platform-subsets.md is generated from
this table (--markdown) and explains it. FreeBSD is not in the first certification (D-077 #4).

  platform_subset.py --platform linux|macos|windows --nift BIN [--build DIR]   run the subset
  platform_subset.py --list --platform P                                        print what would run / be excluded
  platform_subset.py ... --only ID[,ID]                                         a few entries (debugging only)
  platform_subset.py --markdown                                                 the table, for the doc
  platform_subset.py --check [--makefile Makefile]                              self-check of the table

--check: every entry has a reason for each platform it excludes (at least 25 characters, naming a construct, never
just a platform name); every test script of `make test-rt-vm`'s recipe is in the table (so a new suite cannot be
left out silently); every script the table names exists.

The run: each included command runs from the repository root with its placeholders filled ({nift}, {build}, {exe},
{py}); its exit status decides PASS or FAIL. A unit binary that was not built is a FAIL, not a skip. Exit 1 if any
included entry failed.
"""
import argparse
import os
import re
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
PLATFORMS = ("linux", "macos", "windows")

# Reasons reused below (each names the construct that does not exist or does not mean the same on that platform).
W_PREEXEC = ("Windows: the harness passes a trace descriptor as fd 3 with preexec_fn/pass_fds (os.dup2 in the child); "
             "Python's subprocess has no preexec_fn on Windows")
W_SESSION = ("Windows: rt-diff runs each case in its own POSIX session and kills the process group (start_new_session, "
             "os.killpg), and vm_parity bounds the child with RLIMIT_AS/RLIMIT_FSIZE through preexec_fn")
W_SHEBANG = "Windows: the harness writes a `#!/bin/sh` fake executable and chmods it; Windows cannot exec a shebang script"
W_FIFO = "Windows: the cases read from a FIFO created with os.mkfifo, which Windows does not have"
W_RLIMIT = ("Windows: every run is bounded by RLIMIT_AS/RLIMIT_FSIZE (the POSIX-only `resource` module, through "
            "preexec_fn); the bound is the point of the suite")
W_PTY = "Windows: the suite drives nift through a pseudo-terminal (pty.fork) and SIGKILL"
W_ULIMIT = "Windows: every run is wrapped in `ulimit -f/-v` and GNU `timeout`; MSYS2 bash cannot set RLIMIT_AS (-v)"
M_PROC = ("macOS: the harness reads /proc (VmPeak/VmHWM of the child, leftover producers by cmdline, zombies), which "
          "Darwin does not have, and RLIMIT_AS is not enforced by the Darwin kernel, so the address-space bound proves nothing")

# (id, area, command, {platform: exclusion reason}, note). Commands use {nift}, {build}, {exe}, {py}.
ENTRIES = [
    # --- VM compiler and execution: unit tests (test-rt-vm prerequisites) --------------------------------------
    ("rt-vm-unit", "compiler/execution", "{build}/rt/vm-unit{exe}", {}, ""),
    ("rt-vm-globals-unit", "compiler/execution", "{build}/rt/vm-globals-unit{exe}", {}, ""),
    ("rt-vm-modules-unit", "compiler/execution", "{build}/rt/vm-modules-unit{exe}", {}, ""),
    ("rt-vm-prepared-parse", "compiler/execution", "{build}/rt/vm-prepared-parse{exe}", {}, ""),
    ("rt-lint", "compiler/execution", "{py} tools/rt-lint.py src/rt", {}, "source lint, platform independent"),
    ("legacy-names", "compiler/execution", "{py} tools/rt-vm/legacy_names.py --check", {}, ""),
    # --- VM compiler and execution: corpus and generated programs (test-rt-vm recipe) -------------------------
    ("vm_parity", "core semantics", "{py} tests/runtime2/vm_parity.py --nift {nift} --gen 300 --seed 1 --rt-diff",
     {"windows": W_SESSION}, "the VM-vs-legacy corpus differential; on Windows the per-suite entries below cover the VM"),
    ("vm_adversarial_timeout_selftest", "core semantics", "{py} tests/runtime2/vm_adversarial_timeout_selftest.py",
     {"windows": W_SHEBANG + "; it also relies on SIGKILL of the fake"}, ""),
    ("vm_adversarial", "core semantics", "{py} tests/runtime2/vm_adversarial.py --nift {nift} --seed 1 --count 600", {},
     "its machine-wide flock is a no-op where fcntl is missing (Windows)"),
    ("vm_subset", "core semantics", "{py} tests/runtime2/vm_subset.py --nift {nift}", {},
     "its RSS check is skipped where os.wait4 is missing (Windows), by the suite itself"),
    ("vm_subset_gen", "core semantics",
     "{py} tests/runtime2/vm_subset_gen.py --nift {nift} --suite both --seed 1 --count 500", {}, ""),
    ("vm_builtins", "core semantics", "{py} tests/runtime2/vm_builtins.py --nift {nift}", {}, ""),
    ("vm_host", "file operations", "{py} tests/runtime2/vm_host.py --nift {nift}",
     {"windows": W_PREEXEC + "; it also creates POSIX symlinks"}, "macOS already runs it in v44-cross-platform"),
    ("vm_records_coverage", "core semantics", "{py} tests/runtime2/vm_records_coverage.py --nift {nift}", {}, ""),
    ("vm_path_wording", "core semantics", "{py} tests/runtime2/vm_path_wording.py --nift {nift}", {}, ""),
    ("vm_known_refused", "core semantics", "{py} tests/runtime2/vm_known_refused.py --nift {nift}", {}, ""),
    ("vm_engine_selection", "core semantics", "{py} tests/runtime2/vm_engine_selection.py --nift {nift}", {}, ""),
    ("vm_former_replay", "core semantics",
     "{py} tests/runtime2/vm_former_replay.py --nift {nift} --seed 1 --sample 600", {}, ""),
    ("vm_d018", "core semantics", "{py} tests/runtime2/vm_d018.py --nift {nift}", {}, ""),
    ("vm_context_consistency", "core semantics", "{py} tests/runtime2/vm_context_consistency.py --nift {nift}", {}, ""),
    ("vm_policy_negative", "core semantics", "{py} tests/runtime2/vm_policy_negative.py", {}, ""),
    ("vm_review_cp12", "core semantics", "{py} tests/runtime2/vm_review_cp12.py --nift {nift}", {}, ""),
    ("vm_golden", "core semantics", "{py} tests/runtime2/vm_golden.py --nift {nift}", {}, ""),
    ("vm_int_bounds", "core semantics", "{py} tests/runtime2/vm_int_bounds.py --nift {nift}", {}, ""),
    ("vm_inventory", "core semantics", "{py} tests/runtime2/vm_inventory.py --nift {nift}", {}, ""),
    ("vm_chain", "core semantics", "{py} tests/runtime2/vm_chain.py --nift {nift}", {}, ""),
    ("vm_modules", "core semantics", "{py} tests/runtime2/vm_modules.py --nift {nift} --gen 100 --seed 1", {}, ""),
    ("module_host_order", "file operations", "{py} tests/runtime2/module_host_order.py --nift {nift}",
     {"windows": W_PREEXEC}, ""),
    ("vm_structs", "core semantics", "{py} tests/runtime2/vm_structs.py --nift {nift} --gen 100 --seed 1", {}, ""),
    ("vm_globals", "core semantics", "{py} tests/runtime2/vm_globals.py --nift {nift} --gen 200 --seed 1", {}, ""),
    ("semantics_modules", "core semantics", "{py} tests/runtime2/semantics_modules.py --nift {nift}", {}, ""),
    ("semantics_modules-structs", "core semantics",
     "{py} tests/runtime2/semantics_modules.py --nift {nift} --dir semantics-structs", {}, ""),
    ("modules_d041", "core semantics", "{py} tests/runtime2/modules_d041.py --nift {nift} --legacy-today", {}, ""),
    ("cp12b_inventory-selftest", "core semantics", "{py} tools/rt-vm/cp12b_inventory.py selftest",
     {"windows": W_SHEBANG + " (the NIFT_VM=classify-all wrapper)"}, ""),
    # --- memory and mutation safety, reference semantics -------------------------------------------------------
    ("safety", "memory/mutation safety", "{py} tests/runtime2/safety/safety.py --nift {nift}", {}, ""),
    ("safety-fuzz-selftest", "memory/mutation safety", "{py} tests/runtime2/safety/fuzz.py --selftest",
     {"windows": W_SHEBANG + " (fake nift binaries that must fire each check)"}, ""),
    ("safety-fuzz", "memory/mutation safety", "{py} tests/runtime2/safety/fuzz.py --nift {nift} --seed 1", {}, ""),
    ("b3_structs", "memory/mutation safety", "{py} tests/runtime2/b3_structs.py --nift {nift}", {}, ""),
    ("b4_dispatch", "memory/mutation safety", "{py} tests/runtime2/b4_dispatch.py --nift {nift}", {}, ""),
    ("b5-evalorder", "memory/mutation safety",
     "{py} -I tests/runtime2/b5/gen/gen_evalorder.py --nift {nift} --tree .", {}, ""),
    ("b5-evalorder-controls", "memory/mutation safety",
     "{py} -I tests/runtime2/b5/gen/gen_evalorder.py --nift {nift} --negative-controls", {}, ""),
    ("b5-operators", "memory/mutation safety", "{py} -I tests/runtime2/b5/gen/gen_operators.py --nift {nift} --tree .",
     {}, ""),
    ("b5-operators-controls", "memory/mutation safety",
     "{py} -I tests/runtime2/b5/gen/gen_operators.py --nift {nift} --negative-controls", {}, ""),
    ("refsem_a", "memory/mutation safety", "{py} tests/runtime2/refsem_a.py --nift {nift}", {}, ""),
    ("cycle_refusal_routes", "memory/mutation safety", "{py} -B tests/runtime2/cycle_refusal_routes.py --nift {nift}",
     {}, ""),
    ("collections", "memory/mutation safety", "{py} tests/runtime2/weekend_collections.py --nift {nift}", {}, ""),
    ("closures_s1", "memory/mutation safety", "{py} tests/runtime2/closures_s1.py --nift {nift}", {}, ""),
    ("for_s2", "memory/mutation safety", "{py} tests/runtime2/for_s2.py --nift {nift}", {}, ""),
    ("vm_host_memory", "memory/mutation safety", "{py} tests/runtime2/vm_host_memory.py --nift {nift}",
     {"windows": W_FIFO}, ""),
    # --- coroutines and threads ----------------------------------------------------------------------------------
    ("cp22-legacy", "core semantics", "{py} tests/runtime2/cp22/run.py --nift {nift} --mode legacy", {}, ""),
    ("cp22-vm", "core semantics", "{py} tests/runtime2/cp22/run.py --nift {nift} --mode vm", {},
     "the normal binary only: the CP13B switch binary is Linux-campaign only (see the doc)"),
    ("cp22-refuse", "core semantics", "{py} tests/runtime2/cp22/run.py --nift {nift} --mode refuse", {}, ""),
    ("cp22-default", "core semantics", "{py} tests/runtime2/cp22/run.py --nift {nift} --mode default", {}, ""),
    ("cp22-vm-only", "core semantics", "{py} tests/runtime2/cp22/run.py --nift {nift} --mode vm-only", {}, ""),
    ("cp22-trace", "core semantics", "{py} tests/runtime2/cp22/run.py --nift {nift} --mode trace", {}, ""),
    ("cp22-binding", "core semantics", "{py} tests/runtime2/cp22/binding.py --nift {nift}", {}, ""),
    ("cp22-controls", "core semantics", "{py} tests/runtime2/cp22/controls.py --nift {nift}",
     {"windows": W_SHEBANG + " (a dead `#!/bin/sh` callee)"}, ""),
    ("cp22-print_order", "core semantics", "{py} tests/runtime2/cp22/print_order.py --nift {nift}", {},
     "the output-order repair adds Windows std-stream identity code: CI-verified only"),
    ("cp23-compatibility", "core semantics",
     "{py} tests/runtime2/cp23/compatibility.py --nift {nift} --output {build}/cp23-compatibility.json", {}, ""),
    ("coro_module", "core semantics", "{py} tests/runtime2/coro_module/coro_module.py --nift {nift}", {}, ""),
    # --- language surface, admission, diagnostics --------------------------------------------------------------
    ("named_deferrals", "core semantics", "{py} tests/runtime2/named_deferrals.py --nift {nift}", {}, ""),
    ("deadcode_admission", "core semantics", "{py} -B tests/runtime2/deadcode_admission.py --nift {nift}", {}, ""),
    ("args_cmd", "core semantics", "{py} tests/runtime2/args_cmd/args_cmd.py --nift {nift}", {},
     "creates one symlink: windows-latest runs as an administrator, so os.symlink works there"),
    ("import_depth", "core semantics", "{py} tests/runtime2/import_depth.py --nift {nift}", {}, ""),
    ("precise_positions", "core semantics", "{py} tests/runtime2/precise_positions.py --nift {nift}", {}, ""),
    ("capture_origins", "core semantics", "{py} tests/runtime2/capture_origins.py --nift {nift}", {}, ""),
    ("literals", "core semantics", "{py} tests/runtime2/literals/run.py --nift {nift} --oracle {nift}", {}, ""),
    ("literals-decimal", "core semantics",
     "{py} tests/runtime2/literals/decimal_infer.py --nift {nift} --oracle {nift}", {}, ""),
    ("vm_default", "core semantics", "{py} tests/runtime2/vm_default/vm_default.py --nift {nift}", {"windows": W_PTY}, ""),
    ("packages", "core semantics", "{py} tests/runtime2/packages/packages.py --nift {nift}",
     {"windows": "Windows: the suite takes an fcntl lock, creates POSIX symlinks and SIGKILLs a stuck child"}, ""),
    ("diagnostics", "core semantics", "bash tests/runtime2/diagnostics/run.sh --binary {nift}", {},
     "needs GNU timeout(1): the workflow puts coreutils' gnubin on PATH on macOS"),
    ("diagnostics-changes-old", "core semantics",
     "bash tests/runtime2/diagnostics/run.sh --binary {nift} --suite changes --expect old", {}, ""),
    ("diagnostics-changes-new", "core semantics",
     "bash tests/runtime2/diagnostics/run.sh --binary {nift} --suite changes --expect new", {}, ""),
    ("diagnostics-codes", "core semantics", "{py} tests/runtime2/diagnostics/codes_check.py", {}, ""),
    ("diagnostics-json", "core semantics", "{py} tests/runtime2/diagnostics/json_check.py", {}, ""),
    ("d040_witnesses", "core semantics", "{py} tests/runtime2/d040_witnesses_selftest.py", {}, "needs rt-parse-dump"),
    ("cp25r-differential", "core semantics", "bash tests/runtime2/cp25/differential.sh {nift}", {}, ""),
    ("cp25r-post-cert-refusals", "core semantics", "{py} tests/runtime2/cp25/post_cert_refusals.py --nift {nift}", {},
     "D-077 #1 refusal contracts; a missing corpus is NOT RUN (exit 2), never a pass"),
    ("approved_refusal-selftest", "core semantics", "bash tests/runtime2/approved_refusal_selftest.sh {nift}", {}, ""),
    ("rt_parser_corpus", "compiler/execution", "{py} tests/runtime2/rt_parser_corpus.py --build {build} --nift {nift}",
     {"windows": "Windows: the deep-nesting check runs the parser under `ulimit -s 1024` (bash), which MSYS2 cannot set "
                 "for a native process"}, "needs rt-parser-unit, rt-parse-dump and rt-legacy-oracle"),
    # --- file operations -----------------------------------------------------------------------------------------
    ("fs_freedom", "file operations", "bash tests/runtime2/fs_freedom.sh", {},
     "symlinks: the workflow sets MSYS=winsymlinks:nativestrict on Windows"),
    ("dynamic_reads", "file operations", "bash tests/runtime2/dynamic_reads.sh", {}, ""),
    ("dynamic_reads_review", "file operations", "bash tests/runtime2/dynamic_reads_review.sh", {}, ""),
    ("host_deps", "file operations", "{py} tests/runtime2/host_deps.py", {"windows": W_RLIMIT}, ""),
    ("file_copy_move_freedom", "file operations", "bash tests/runtime2/file_copy_move_freedom.sh", {"windows": W_ULIMIT},
     "macOS: CI-verified (Darwin may reject `ulimit -v`)"),
    ("build_write_ownership", "file operations", "bash tests/runtime2/build_write_ownership.sh", {"windows": W_ULIMIT},
     "macOS: CI-verified (Darwin may reject `ulimit -v`)"),
    ("read_limit", "file operations", "{py} tests/runtime2/read_limit.py",
     {"macos": M_PROC, "windows": W_RLIMIT + "; it also needs /proc, mkfifo, /dev/zero and /dev/full"},
     "D-077 #3 contract; Linux certifies the read limit"),
    ("read_limit_controls", "file operations", "{py} -B tests/runtime2/read_limit_controls.py",
     {"macos": M_PROC + "; the paced FIFO uses FIONREAD (termios)",
      "windows": W_RLIMIT + "; it also needs mkfifo, fcntl/termios and /dev/zero"}, ""),
    ("read_limit_contract_controls", "file operations", "{py} -B tests/runtime2/read_limit_contract_controls.py",
     {"windows": W_RLIMIT}, "macOS: CI-verified (Darwin may ignore RLIMIT_AS; the controls do not depend on it)"),
]
# Make targets the workflow runs per platform beside this table (existing recipes; each listed with its exclusions
# in docs/runtime-2/platform-subsets.md): embedding and the v4.4/v4.10 contracts already split in the existing
# workflows are not repeated here.
MAKE_TARGETS = [
    ("test-v45-embed-contracts", "embedding", {}, "already run on all three in v44-cross-platform"),
    ("test-rt-value", "memory/mutation safety", {}, "value layer, heap budget and OOM units"),
    ("test-runtime2-kind-bits", "compiler/execution", {}, ""),
    ("test-rt-source-map", "compiler/execution", {}, ""),
]
BUILDS = ["{build}/rt/vm-unit{exe}", "{build}/rt/vm-globals-unit{exe}", "{build}/rt/vm-modules-unit{exe}",
          "{build}/rt/vm-prepared-parse{exe}", "{build}/rt-parse-dump{exe}", "{build}/rt-parser-unit{exe}",
          "{build}/rt-legacy-oracle{exe}"]


def fill(cmd, a):
    return cmd.format(nift=shlex.quote(a.nift), build=a.build, exe=".exe" if a.platform == "windows" else "",
                      py=shlex.quote(sys.executable))


def check(makefile):
    problems = []
    ids = [e[0] for e in ENTRIES]
    if len(ids) != len(set(ids)):
        problems.append("duplicate ids")
    for eid, _area, cmd, excl, _note in ENTRIES + [(t, a, t, x, n) for t, a, x, n in MAKE_TARGETS]:
        for plat, why in excl.items():
            if plat not in PLATFORMS:
                problems.append("%s: unknown platform %s" % (eid, plat))
            if len(why) < 25 or why.strip().lower().rstrip(".") in PLATFORMS:
                problems.append("%s: the %s exclusion has no concrete reason" % (eid, plat))
        if set(excl) >= set(PLATFORMS):
            problems.append("%s: excluded everywhere (remove it, or record it as a Linux-campaign gate)" % eid)
        for path in re.findall(r"(?:tests|tools)/[\w./-]+\.(?:py|sh)", cmd):
            if not os.path.exists(os.path.join(REPO, path)):
                problems.append("%s: %s does not exist" % (eid, path))
    text = open(makefile).read()
    m = re.search(r"^test-rt-vm: test-runtime2-vm-corpus-complete.*?\n((?:\t.*\n)+)", text, re.M)
    if not m:
        problems.append("the test-rt-vm recipe was not found in %s" % makefile)
    else:
        named = " ".join(e[2] for e in ENTRIES)
        for script in sorted(set(re.findall(r"(?:tests|tools)/[\w./-]+\.py", m.group(1)))):
            if script not in named:
                problems.append("test-rt-vm runs %s, which the platform table does not list" % script)
    return problems


def markdown():
    out = ["| Entry | Area | Linux | macOS | Windows | Note |", "|---|---|---|---|---|---|"]
    for eid, area, cmd, excl, note in ENTRIES:
        cells = ["**excluded**: %s" % excl[p] if p in excl else "run" for p in PLATFORMS]
        out.append("| `%s` | %s | %s | %s | %s | %s |" % (eid, area, cells[0], cells[1], cells[2], note.replace("|", "\\|")))
    for target, area, excl, note in MAKE_TARGETS:
        cells = ["**excluded**: %s" % excl[p] if p in excl else "run" for p in PLATFORMS]
        out.append("| `make %s` | %s | %s | %s | %s | %s |" % (target, area, cells[0], cells[1], cells[2], note))
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", choices=PLATFORMS)
    ap.add_argument("--nift")
    ap.add_argument("--build", default=".build")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--makefile", default=os.path.join(REPO, "Makefile"))
    ap.add_argument("--only", help="comma-separated entry ids (debugging; the certified run takes the whole table)")
    a = ap.parse_args()
    if a.markdown:
        sys.stdout.write(markdown())
        return 0
    if a.check:
        problems = check(a.makefile)
        for p in problems:
            print("FAIL platform subset table:", p)
        print("%s platform subset table: %d entries, %d make targets" %
              ("FAIL" if problems else "PASS", len(ENTRIES), len(MAKE_TARGETS)))
        return 1 if problems else 0
    if not a.platform or (not a.list and not a.nift):
        ap.error("--platform and --nift are required to run")
    a.nift = os.path.abspath(a.nift) if a.nift else "NIFT"
    failed, ran, excluded = [], 0, 0
    only = set(a.only.split(",")) if a.only else None
    for eid, area, cmd, excl, _note in ENTRIES:
        if only is not None and eid not in only:
            continue
        if a.platform in excl:
            excluded += 1
            print("EXCLUDED %s on %s (%s)" % (eid, a.platform, excl[a.platform]))
            continue
        line = fill(cmd, a)
        if a.list:
            print("RUN %s: %s" % (eid, line))
            continue
        print("=== %s (%s): %s" % (eid, area, line), flush=True)
        argv = shlex.split(line)
        if argv[0].startswith(a.build + "/"):
            # Absolute: Windows resolves a relative executable against the parent's directory, not cwd.
            argv[0] = os.path.join(REPO, argv[0])
            if not os.path.exists(argv[0]):
                print("FAIL %s: unit binary %s was not built (a missing build is a failure, not a skip)" % (eid, argv[0]))
                failed.append(eid)
                continue
        env = dict(os.environ, NIFT_BIN=a.nift, PYTHONDONTWRITEBYTECODE="1")
        rc = subprocess.run(argv, cwd=REPO, env=env).returncode
        print("%s %s (exit %d)" % ("PASS" if rc == 0 else "FAIL", eid, rc), flush=True)
        ran += 1
        if rc != 0:
            failed.append(eid)
    if a.list:
        return 0
    print("platform subset %s: %d run, %d excluded with a recorded reason, %d failed%s" %
          (a.platform, ran, excluded, len(failed), (": " + ", ".join(failed)) if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
