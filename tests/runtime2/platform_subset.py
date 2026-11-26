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
{py}); its exit status decides PASS or FAIL. An entry that exits 0 is PARTIAL, not PASS, when it printed a skip that
certify's own classifier (scripts/certify.py skipped_assertions) counts as required and that EXPECTED_SKIPS does not
list for that platform and entry (review L3, N1); design-boundary lines are NOTEs. --selftest checks this. A unit binary that was not built is a FAIL, not a skip. Exit 1 if any included entry
failed, else 2 if any was PARTIAL.
"""
import argparse
import os
import re
import shlex
import subprocess
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
PLATFORMS = ("linux", "macos", "windows", "freebsd")

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
    ("vm_package_process", "process execution",
     "{py} tests/runtime2/vm_package_process.py --nift {nift}", {}, ""),
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
    ("named_deferrals", "core semantics", "{py} tests/runtime2/named_deferrals.py --nift {nift}",
     {p: "%s: named_deferrals.py pins its refusals with Linux-only programs (FFI through libc.so.6, an `echo` "
         "executable) and on any other platform prints one SKIP line and exits 0 without checking anything" % n
      for p, n in (("macos", "macOS"), ("windows", "Windows"), ("freebsd", "FreeBSD"))}, ""),
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
     {"macos": M_PROC, "windows": W_RLIMIT + "; it also needs /proc, mkfifo, /dev/zero and /dev/full",
      "freebsd": "FreeBSD: the full harness requires Linux /proc/self/status, /proc/meminfo, /proc/self/mem and per-PID procfs checks"},
     "D-077 #3 contract; Linux certifies the read limit"),
    ("read_limit_controls", "file operations", "{py} -B tests/runtime2/read_limit_controls.py",
     {"macos": M_PROC + "; the paced FIFO uses FIONREAD (termios)",
      "freebsd": "FreeBSD: this harness imports Linux procfs producer, zombie and VmHWM checks from read_limit.py",
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
    # D-090 (owner 2026-11-14): the certification guard is a required gate of the first certification.
    ("test-runtime2-certification-guard", "certification integrity",
     {"macos": "macOS: the guard reads the GNU make database of the whole certify plan through scripts/certify.py; it certifies the plan itself, which is platform independent, so the Linux job runs it (D-090)", "windows": "Windows: scripts/certify.py imports the POSIX-only pwd module and the guard drives GNU make -p and bash over the POSIX plan; the plan is platform independent and the Linux job runs it (D-090)"},
     "required (D-090); FAILED since C140c (three unlisted skip phrases in tests/runtime2/suite_status.sh); expected PASS once the candidate fix lands"),
]
# Review N1: environmental skips expected on a platform, per entry: (regex on the skip line, reason). A skip line
# is classified by certify's own classifier (scripts/certify.py skipped_assertions: SKIP_MARKERS, ZERO_SKIPS,
# KNOWN_CONDITIONAL). A line it marks not required (a documented design boundary or another platform's message) is a
# NOTE; a required skip listed here for this platform and entry is an EXPECTED SKIP; any other skip makes the entry
# PARTIAL (an unexpected skip), never PASS. docs/runtime-2/platform-subsets.md lists these.
EXPECTED_SKIPS = {
    ("macos", "file_copy_move_freedom"): [
        (r"^SKIP cross-device move \(/dev/shm missing or on the same filesystem\)$",
         "Darwin has no /dev/shm: the cross-device move cases need a second (tmpfs) filesystem")],
    ("macos", "build_write_ownership"): [
        (r"^SKIP cross-device move cases \(/dev/shm missing or on the same filesystem\)$",
         "Darwin has no /dev/shm: the cross-device move cases need a second (tmpfs) filesystem")],
}
BUILDS = ["{build}/rt/vm-unit{exe}", "{build}/rt/vm-globals-unit{exe}", "{build}/rt/vm-modules-unit{exe}",
          "{build}/rt/vm-prepared-parse{exe}", "{build}/rt-parse-dump{exe}", "{build}/rt-parser-unit{exe}",
          "{build}/rt-legacy-oracle{exe}"]


def fill(cmd, a):
    return cmd.format(nift=shlex.quote(a.nift), build=a.build, exe=".exe" if a.platform == "windows" else "",
                      py=shlex.quote(sys.executable))


def skip_classifier():
    """certify's own skip classification (scripts/certify.py), not a copy of it. certify imports `pwd` at module level
    and the classifier does not use it, so a stand-in module is installed where `pwd` does not exist (Windows)."""
    sys.path.insert(0, os.path.join(REPO, "scripts"))
    try:
        import pwd  # noqa: F401
    except ImportError:
        sys.modules["pwd"] = types.ModuleType("pwd")
    import certify  # noqa: E402
    return certify.skipped_assertions


def classify(platform, eid, rc, text, skipped_assertions):
    """(status, notes): FAIL on a non-zero exit; else PARTIAL on any unexpected skip; else PASS. notes lists every
    skip line with its class (NOTE design boundary, EXPECTED SKIP, UNEXPECTED SKIP)."""
    notes, unexpected = [], 0
    expected = EXPECTED_SKIPS.get((platform, eid), [])
    for line, requirement, required in skipped_assertions(text):
        if required is not True:
            notes.append("NOTE %s: %s (%s)" % (eid, line, requirement))
        elif any(re.search(rx, line) for rx, _ in expected):
            notes.append("EXPECTED SKIP %s on %s: %s" % (eid, platform, line))
        else:
            unexpected += 1
            notes.append("UNEXPECTED SKIP %s: %s (%s)" % (eid, line, requirement))
    if rc != 0:
        return "FAIL", notes
    return ("PARTIAL" if unexpected else "PASS"), notes


def check(makefile):
    problems = []
    for (plat, eid), rows in EXPECTED_SKIPS.items():
        entry = next((e for e in ENTRIES if e[0] == eid), None)
        if plat not in PLATFORMS or entry is None or plat in entry[3]:
            problems.append("expected skip for %s on %s: no such entry run on that platform" % (eid, plat))
        if any(len(why) < 25 for _, why in rows):
            problems.append("expected skip for %s on %s has no concrete reason" % (eid, plat))
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
    return problems + recipe_problems(open(makefile).read(), makefile)


# The test-rt-vm recipe: its head names test-runtime2-vm-corpus-complete among its prerequisites, wherever it stands
# (C140c added test-rt-vm-invalidate before it, D-103 dry run R2); the recipe is the tab-indented lines after it.
RECIPE = re.compile(r"^test-rt-vm:(?:[^\n]*[ \t])?test-runtime2-vm-corpus-complete(?=[ \t]|$)[^\n]*\n((?:\t.*\n)+)", re.M)


def recipe_problems(text, makefile="Makefile"):
    """Every test script the test-rt-vm recipe runs is in the table; a missing recipe is a problem, never a pass."""
    problems = []
    m = RECIPE.search(text)
    if not m:
        problems.append("the test-rt-vm recipe was not found in %s" % makefile)
    else:
        named = " ".join(e[2] for e in ENTRIES)
        for script in sorted(set(re.findall(r"(?:tests|tools)/[\w./-]+\.py", m.group(1)))):
            if script not in named:
                problems.append("test-rt-vm runs %s, which the platform table does not list" % script)
    return problems


def markdown():
    out = ["| Entry | Area | Linux | macOS | Windows | FreeBSD | Note |", "|---|---|---|---|---|---|---|"]
    for eid, area, cmd, excl, note in ENTRIES:
        cells = ["**excluded**: %s" % excl[p] if p in excl else "run" for p in PLATFORMS]
        out.append("| " + " | ".join((f"`{eid}`", area, *cells, note.replace("|", "\\|"))) + " |")
    for target, area, excl, note in MAKE_TARGETS:
        cells = ["**excluded**: %s" % excl[p] if p in excl else "run" for p in PLATFORMS]
        out.append("| " + " | ".join((f"`make {target}`", area, *cells, note)) + " |")
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
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
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
    return run_table(ENTRIES, a)


def run_table(entries, a):
    skipped_assertions = None if a.list else skip_classifier()
    failed, partial, blocked, ran, excluded = [], [], [], 0, 0
    only = set(a.only.split(",")) if a.only else None
    for eid, area, cmd, excl, _note in entries:
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
        if argv[0] == "bash":
            argv[0] = os.environ.get("NIFT_TEST_BASH", "bash")
        if argv[0].startswith(a.build + "/"):
            # Absolute: Windows resolves a relative executable against the parent's directory, not cwd.
            argv[0] = os.path.join(REPO, argv[0])
            if not os.path.exists(argv[0]):
                if a.platform == "freebsd":
                    print("BLOCKED %s: unit binary %s was not built" % (eid, argv[0]))
                    blocked.append(eid)
                    continue
                print("FAIL %s: unit binary %s was not built (a missing build is a failure, not a skip)" % (eid, argv[0]))
                failed.append(eid)
                continue
        env = dict(os.environ, NIFT_BIN=a.nift, PYTHONDONTWRITEBYTECODE="1")
        # Review L3/N1: an entry that exits 0 with an unexpected skip (certify's classification) is PARTIAL, never
        # PASS. Its output is passed through unchanged.
        try:
            proc = subprocess.Popen(argv, cwd=REPO, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        except OSError as error:
            if a.platform != "freebsd":
                raise
            print("BLOCKED %s: %s" % (eid, error))
            blocked.append(eid)
            continue
        out = []
        for raw in proc.stdout:
            text = raw.decode(errors="replace")
            sys.stdout.write(text)
            out.append(text)
        rc = proc.wait()
        sys.stdout.flush()
        ran += 1
        status, notes = classify(a.platform, eid, rc, "".join(out), skipped_assertions)
        for n in notes:
            print(n)
        label = status
        if a.platform == "freebsd" and (status == "PARTIAL" or rc == 77):
            label = "NOT RUN"
            status = "PARTIAL"
        print("%s %s (exit %d)" % (label, eid, rc), flush=True)
        if status == "FAIL":
            failed.append(eid)
        elif status == "PARTIAL":
            partial.append(eid)
    if a.list:
        return 0
    print("platform subset %s: %d run, %d excluded with a recorded reason, %d failed%s, %d partial%s" %
          (a.platform, ran, excluded, len(failed), (": " + ", ".join(failed)) if failed else "",
           len(partial), (": " + ", ".join(partial)) if partial else ""))
    if a.platform == "freebsd":
        print("FreeBSD outcomes: PASS %d, FAIL %d, NOT RUN %d, EXCLUDED %d, BLOCKED %d" %
              (ran - len(failed) - len(partial), len(failed), len(partial), excluded, len(blocked)))
    if failed or blocked:
        return 1
    return 2 if partial else 0


def selftest():
    """Review N1: the run's classification, on certify's real classifier (needs scripts/certify.py)."""
    sa = skip_classifier()
    failures = []

    def expect(what, got, want):
        if got != want:
            failures.append("%s: got %s, want %s" % (what, got, want))
    boundary = "SKIP [modified] edit during streaming is outside the v4.12 modified-mode contract"
    expect("design-boundary line", classify("linux", "host_deps", 0, "PASS a\n%s\n" % boundary, sa)[0], "PASS")
    for line in ("SKIP something unknown", "SKIPPED - IPv4 unavailable here", "skipped: frobnicate case",
                 "ok 3 # SKIP no tty", "NOT RUN read_line on a directory"):
        expect("skip line %r" % line, classify("linux", "vm_golden", 0, "PASS a\n%s\n" % line, sa)[0], "PARTIAL")
    expect("zero-skip summary", classify("linux", "vm_golden", 0, "ran 5, 0 skipped\n", sa)[0], "PASS")
    shm = "SKIP cross-device move (/dev/shm missing or on the same filesystem)"
    expect("expected skip on macOS", classify("macos", "file_copy_move_freedom", 0, shm + "\n", sa)[0], "PASS")
    expect("same skip on Linux", classify("linux", "file_copy_move_freedom", 0, shm + "\n", sa)[0], "PARTIAL")
    expect("failure wins", classify("macos", "file_copy_move_freedom", 1, shm + "\n", sa)[0], "FAIL")
    # An excluded entry is reported and never run: its command would create a marker.
    with tempfile.TemporaryDirectory(prefix="platform-subset-selftest-") as d:
        marker = os.path.join(d, "ran")
        cmd = "{py} -c \"open(%r, 'w').close()\"" % marker
        entries = [("excluded-probe", "t", cmd, {"macos": "macOS: a probe excluded to prove that it never runs"}, ""),
                   ("included-probe", "t", "{py} -c \"print('ok')\"", {}, "")]
        a = types.SimpleNamespace(platform="macos", nift="/bin/true", build=".build", list=False, only=None)
        rc = run_table(entries, a)
        expect("excluded entry run", os.path.exists(marker), False)
        expect("run with only an excluded and a passing entry", rc, 0)
    # D-103 R2: the recipe check reads both recipe heads, and still fails on an unlisted suite or a missing recipe.
    old_head = "test-rt-vm: test-runtime2-vm-corpus-complete $(RT_VM_UNIT)\n\tpython3 tests/runtime2/vm_golden.py --nift x\n"
    c140c = ("test-rt-vm: test-rt-vm-invalidate test-runtime2-vm-corpus-complete $(RT_VM_UNIT)\n"
             "\t-@$(RT_VM_SUITE) vm-golden -- python3 tests/runtime2/vm_golden.py --nift x\n")
    unlisted = c140c + "\t-@$(RT_VM_SUITE) new -- python3 tests/runtime2/brand_new_suite.py --nift x\n"
    decoy = "test-rt-vm: test-runtime2-vm-corpus-complete-old\n\tpython3 tests/runtime2/brand_new_suite.py\n"
    expect("recipe, the pre-C140c head", recipe_problems(old_head), [])
    expect("recipe, the C140c head (test-rt-vm-invalidate first)", recipe_problems(c140c), [])
    expect("recipe, an unlisted suite", recipe_problems(unlisted),
           ["test-rt-vm runs tests/runtime2/brand_new_suite.py, which the platform table does not list"])
    expect("recipe, missing", recipe_problems("all: nift\n\techo\n"),
           ["the test-rt-vm recipe was not found in Makefile"])
    expect("recipe, a longer target name is not the prerequisite", recipe_problems(decoy),
           ["the test-rt-vm recipe was not found in Makefile"])
    from fixture_paths import replace_fixture_root, strip_fixture_root
    fixture = os.path.abspath("fixture directory")
    reported = fixture.replace(os.sep, "/")
    message = f"error: {reported}/p.f:5:2: bad operand"
    unrelated = f"error: {reported}-other/p.f:5:2: bad operand"
    expect("strip only the fixture prefix", strip_fixture_root(message, fixture), "error: p.f:5:2: bad operand")
    expect("normalize exact fixture path in bytes", replace_fixture_root(message.encode(), fixture, "$ROOT"),
           b"error: $ROOT/p.f:5:2: bad operand")
    expect("retain unrelated paths that share a prefix", replace_fixture_root(unrelated, fixture, "$ROOT"), unrelated)
    for f in failures:
        print("FAIL platform subset selftest:", f)
    print("%s platform subset selftest: %d check(s)" % ("FAIL" if failures else "PASS", 20))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
