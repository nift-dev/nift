#!/usr/bin/env python3
"""Runtime 2.0 checkpoint certification runner (D-010, docs/runtime-2/certification.md).

Expands the `certify-targets` aggregate into its leaf make targets, runs the
parallel-safe ones concurrently and the exclusive ones serially, re-runs every
failure alone, and classifies each target:

  PASS                  passed on the first run
  FLAKY                 failed in the concurrent run, passed alone (load-sensitive);
                        reported, never counted as a clean pass
  SKIPPED-ENVIRONMENTAL failed only because of the execution environment, shown by
                        re-running: e.g. fails as root but passes as an
                        unprivileged user
  UNVERIFIED            a dependency or platform is unavailable here (detected from
                        the environment AND the log: missing tool, blocked toolchain
                        download, missing sibling checkout); CI covers these
  BASELINE-FAILURE      fails on the v4.12.0 baseline tree at the same failing step,
                        run as the same user as the candidate result it is
                        compared with (pre-existing, not a Runtime 2.0 regression)
  FAIL                  anything else, including a baseline failure at a different
                        or unidentified step

The policy is the pure function decide(), table-tested in
tests/runtime2/certification_guard.sh.

Combined-candidate harness (docs/runtime-2/checkpoints/combined-b3-b5.md, owner 2026-11-07
18:00; C1-C3):

  C1 the plan is certify-targets PLUS the Runtime 2.0 inventory derived from the Make database
     (inventory(): every defined target named like a Runtime 2.0 gate), minus NON_GATE (each with
     its reason); MANDATORY lists the owner-named B3-B5 gates, which must be planned. --check-plan
     fails (exit 1) when an inventory target is neither planned nor in NON_GATE, a NON_GATE entry
     is not a defined target, or a MANDATORY gate is not planned; --drop T removes T from the plan
     first (the guard's negative control).
  C2 an umbrella (a target with a recipe AND gate prerequisites) is expanded: each gate
     prerequisite is its own entry, and the umbrella's own recipe is an entry run with its gate
     prerequisites marked old (`make -k T -o P ...`), after the others. Every entry runs with
     `make -k`, so a known failure stops nothing else. Each required gate is reported in one class:
       PASSED           passed (PASS)
       FAILED-NEW       failed, and not shown identical on a named baseline (FAIL, FLAKY: the
                        zero-flakiness policy counts a flaky gate as failed)
       FAILED-BASELINE  the identical failure (same step, same FAIL lines) on a named baseline:
                        v4.12.0 (--baseline, legacy compatibility) or a regression baseline
                        (--regression-baseline NAME=TREE: the accepted B4b 17f7964c and weekend/r2),
                        reported per baseline and never merged
       BLOCKED          a dependency or the harness is missing (UNVERIFIED, SKIPPED-ENVIRONMENTAL, a
                        harness-blocked log: no CP19 legacy oracle, comparator coverage skipped)
       NOT-RUN          planned and not run
     Only PASSED is a pass. Approved behavioural differences are not a runner baseline: they are
     the B5 expected-output table (docs/runtime-2/evidence/b5/expected-changes.tsv), checked by the
     gates themselves.
  C3 preflight (preflight_problems(), table-tested): GCC 15.2, __GLIBCXX__ == 20260321, comparator
     sort compiled into the production binary (a comparator sort runs on the VM),
     NIFT_CP14B_REQUIRE_COMPARATOR=1, and an explicit v4.12.0 legacy binary (--legacy-oracle or
     CP19_LEGACY_ORACLE; it must report v4.12.0) for test-cp19-proof-cli. A full campaign with a
     preflight problem exits 2 before running anything; any comparator gate that reports SKIP
     fails the campaign.

Exit status: 2 preflight failed (full campaign) or nothing planned; 1 when any required gate is
FAILED-NEW or a comparator gate skipped; 3 when none failed but some are BLOCKED or NOT-RUN (not
certified); 0 when every required gate is PASSED or FAILED-BASELINE (--strict: only PASSED). Logs:
.build/certify/<target>.log; summary: .build/certify/summary.json and summary.txt.
"""
import argparse, concurrent.futures as cf, json, os, pwd, re, shutil, signal, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AGGREGATE = "certify-targets"

# Targets that must not run concurrently with others. A target is also treated
# as exclusive automatically when its recipe runs a sub-make (shared build
# outputs) — see recipe_runs_make().
EXCLUSIVE = {
    "test-runtime2-cp18-sanitize": "compiles both VM dispatches and runs allocation-failure controls",
    "test-build-boundary-nondestructive": "snapshots the whole checkout before/after; any concurrent writer is reported as a modification",
    "test-build-boundary": "builds from a clean copy of the checkout; reads the tree while others write it",
    # Isolation audit (docs/runtime-2/certification.md): these recipes write
    # shared outputs outside .build/ (embedding objects, libnift_c.a, the
    # staged dist/embed-prefix, binding build products) that other targets
    # also build or read.
    "test-cp18-bytes": "rebuilds src/embed/*.o in the source tree",
    "test-cp21-bytes": "rebuilds libnift_c.a in the source tree",
    "test-v45-embed-staged-consumer": "restages dist/embed-prefix (shared with the binding targets)",
    "test-go-binding": "restages dist/embed-prefix; builds bindings/go products",
    "test-csharp-binding": "restages dist/embed-prefix; builds bindings/csharp products",
    "test-node-binding": "builds bindings/node/build (shared native module)",
    "test-python-binding": "builds the bindings/python extension module",
    "test-minify": "writes minifypp/.build via a sub-make",
    # Tooling checkpoint (docs/runtime-2/checkpoints/tooling.md 1, 3): these
    # rebuild the shared in-tree objects under other configurations, or plant
    # reproducers into production sources, through a script (no make on the
    # recipe line, so recipe_runs_make() does not see it).
    "test-build-reuse": "rebuilds the shared in-tree objects under other compilers, flags and defines (A->B->A)",
    "test-rt-isolation": "plants reproducers into production sources and rebuilds their objects",
    "test-rt-sort-forgery": "plants the forged-header reproducer into src/Hooks.cpp and relinks nift",
}

# Load-sensitive measurements (owner review of D-012): targets whose pass/fail
# depends on wall-clock upper bounds. Their thresholds are unchanged; they run
# serially, with no competing target, so that a failure reflects the product and
# not the scheduler. A pass under exclusive execution is the evidence; an
# intermittent failure is never recorded as resolved by a later passing run.
LOAD_SENSITIVE = {
    "test-v44-language-foundation": "tests/v44_perf_scaling_smoke.sh: doubling n must not quadruple wall time (map/set/push)",
    "test-v48-map-set-scaling": "tests/v48_map_set_scaling_guard.sh: wall-clock scaling ratio",
    "test-scripting-perf": "tests/scripting_perf_guard.sh: wall-clock budgets",
    "test-runtime2-process-capture": "process_capture.sh: background descendant must not block (<280 ms)",
    "test-load-sensitive": "the four wall-clock scripts above, serially (CI's exclusive step)",
    "test-v43-language": "benchmarks/*_scaling.py: wall-clock scaling ratios (hierarchy, typed content, project dependencies) after the v4.3 smokes",
}
for _t, _why in LOAD_SENSITIVE.items():
    EXCLUSIVE.setdefault(_t, "load-sensitive: " + _why)

# Implementation-time tier (--quick): fast, broad smoke of the runtime and the
# Runtime 2.0 gates. Checkpoint certification always runs the full set.
QUICK = [
    "test-runtime2-certification-guard", "test-runtime2-vm-corpus", "test-runtime2-modified-race",
    "test-runtime2-dynamic-reads", "test-runtime2-fs-freedom", "test-conformance", "test-contracts",
    "test-requirements", "test-dependency-status", "test-call-semantics", "test-control-flow",
]

TEST_NAME = re.compile(r"^(test|bh|checkpoint-[0-9])[A-Za-z0-9_.-]*$")

# C1: the Runtime 2.0 gate inventory is derived from the Make database, not listed by hand: every
# defined target whose name starts with test-, cp25r or vm-, contains runtime2, -rt, cp or vm, and
# is not a 4.x-line test-v4* target (the B5 coverage reconciliation's rule, b5-refsem.md 11.10;
# 244 targets at fc987ec1). A new target that matches is required automatically.
INVENTORY_NAME = re.compile(r"^(test-|cp25r|vm-)")
INVENTORY_PART = re.compile(r"runtime2|-rt|cp|vm")
# Inventory targets that are not gates, each with its reason. Kept short; the guard fails on a
# stale entry.
NON_GATE = {
    "test-runtime2-closures-s1-prepare": "maintenance mode (--prepare): regenerates the CP12C S1 suite's inputs",
    "test-runtime2-closures-s1-stress-prepare": "maintenance mode (--prepare): regenerates the S1 stress inputs",
}
# The owner-named B3-B5 gates (REVIEW-COMBINED.md C1, LINODE-GATES.tsv, owner 2026-11-07 18:00)
# and the combined-candidate gates; each must be planned (checked by --check-plan).
MANDATORY = [
    "test-runtime2-b3", "test-runtime2-b3-mutants", "test-runtime2-b4", "test-runtime2-b4-mutants",
    "test-runtime2-b4b", "test-runtime2-b4b-mutants", "test-runtime2-b4b-fixpoint", "test-runtime2-b4b-work",
    "test-rt-b4b-types-unit", "test-runtime2-b4b-gen-alias",
    "test-runtime2-b5", "test-runtime2-b5-mutants", "test-runtime2-b5-generators", "test-runtime2-b5-evalorder",
    "test-runtime2-b5-evalorder-sanitize", "test-runtime2-b5-snapshot-memory",
    "test-runtime2-guardoff", "test-runtime2-guardoff-sanitize", "test-cp19-proof-cli",
    "test-runtime2-cp14b-design-mutants", "test-runtime2-diagnostics", "test-runtime2-scanner-parity",
    "test-runtime2-prevalidate-gate", "test-runtime2-capture-origins", "test-runtime2-cp12c-probes",
    "test-runtime2-refsem", "test-runtime2-refsem-mutants", "test-runtime2-cp22", "test-runtime2-cp23",
    "test-rt-vm", "test-rt-vm-sanitize", "test-runtime2-certification-guard",
]
# Wall-clock targets of the inventory: run alone, like LOAD_SENSITIVE (Linode performance lane).
PERF_LANE = {
    "test-cp20-launch-bench": "wall-clock launch benchmark",
    "test-cp21-prompt-latency": "wall-clock prompt latency",
    "test-rt-builtins-guards": "wall-clock builtin guard budgets",
    "test-runtime2-for-s2-bench": "wall-clock snapshot-loop benchmark",
    "test-runtime2-literals-scaling": "wall-clock literal scaling",
}
for _t, _why in PERF_LANE.items():
    EXCLUSIVE.setdefault(_t, "performance lane: " + _why)

# C2: log evidence that a gate was blocked by its harness or a missing dependency (BLOCKED, never a
# pass and never a candidate failure). COMPARATOR_SKIP also fails the campaign (C3).
COMPARATOR_SKIP = "SKIP UNVERIFIED COMPARATOR COVERAGE"
HARNESS_BLOCKED = [
    (re.compile(r"CP19_LEGACY_ORACLE is unset|CP19_LEGACY_ORACLE=\S* is not an executable"),
     "no CP19 legacy oracle (v4.12.0 binary): pass --legacy-oracle"),
    (re.compile(re.escape(COMPARATOR_SKIP)), "comparator coverage skipped: not GCC 15.2 with __GLIBCXX__ 20260321"),
]


def make_db(cwd, target):
    out = subprocess.run(["make", "-pqn", target], cwd=cwd, capture_output=True, text=True,
                         env=clean_env()).stdout.split("\n")
    deps, recipe = {}, {}
    cur, in_recipe = None, False
    for line in out:
        m = re.match(r"^([A-Za-z0-9_./+-]+):(?![=:])\s*(.*)$", line)
        if m and not line.startswith("#"):
            cur, in_recipe = m.group(1), False
            deps.setdefault(cur, []).extend(m.group(2).split())
            recipe.setdefault(cur, "")
            continue
        if cur and line.startswith("#  recipe to execute"):
            in_recipe = True
            continue
        if cur and in_recipe and line.startswith("\t"):
            recipe[cur] += line[1:] + "\n"
            continue
        if line == "":
            cur, in_recipe = None, False
    return deps, recipe


def leaves(deps, recipe, root):
    out, seen = [], set()

    def walk(t):
        if t in seen:
            return
        seen.add(t)
        if not recipe.get(t) and deps.get(t):
            for d in deps[t]:
                walk(d)
        elif TEST_NAME.match(t) or t.endswith("-binding"):
            out.append(t)
    walk(root)
    return out


def inventory(deps):
    """C1: the Runtime 2.0 gates defined in the Make database (the derivation rule above)."""
    return sorted(t for t in deps if INVENTORY_NAME.match(t) and INVENTORY_PART.search(t)
                  and not t.startswith("test-v4"))


def plan(deps, recipe, roots):
    """C2: the entries of `roots` in Make's prerequisite order: (target, kind, old). kind "leaf" is a
    gate with a recipe and no gate prerequisites; kind "own" is an umbrella's own recipe, run with
    its gate prerequisites `old` (make -o), which are entries of their own. Also returns every
    target visited (aggregates included), for the inventory check."""
    entries, seen, rootset = [], set(), set(roots)

    def gate(d):
        return TEST_NAME.match(d) or d.endswith("-binding")

    def walk(t):
        if t in seen:
            return
        seen.add(t)
        sub = [d for d in deps.get(t, []) if gate(d)]
        for d in (sub if recipe.get(t) else deps.get(t, [])):  # an aggregate: every prerequisite
            walk(d)
        if recipe.get(t) and (gate(t) or t in rootset):
            entries.append((t, "own" if sub else "leaf", tuple(dict.fromkeys(sub))))
    for r in roots:
        walk(r)
    return entries, seen


def inventory_problems(covered, inv, non_gate, mandatory, defined):
    """C1, pure (table-tested by the guard): every inventory target is planned (covered) or a
    named non-gate; every non-gate is a defined target; every mandatory gate is planned."""
    out = []
    for t in inv:
        if t not in covered and t not in non_gate:
            out.append(f"inventory target {t} is not in the certification plan (plan it, or list it in NON_GATE with a reason)")
    for t in sorted(non_gate):
        if t not in defined:
            out.append(f"NON_GATE entry {t} is not a defined Make target (stale)")
        elif t in covered:
            out.append(f"NON_GATE entry {t} is planned anyway (remove it from NON_GATE)")
    for t in mandatory:
        if t not in defined:
            out.append(f"mandatory gate {t} is not a defined Make target")
        elif t not in covered:
            out.append(f"mandatory gate {t} is not in the certification plan")
    return out


def report_class(status, regression=None, blocked=None):
    """C2, pure (table-tested): the owner's report class of one required gate. `status` is
    decide()'s verdict (or None: not run); `regression` maps each regression baseline to whether it
    shows the identical failure; `blocked` is a harness/dependency reason from the log."""
    if status is None:
        return "NOT-RUN"
    if status == "PASS" and not blocked:
        return "PASSED"
    if blocked or status in ("UNVERIFIED", "SKIPPED-ENVIRONMENTAL"):
        return "BLOCKED"
    if status == "BASELINE-FAILURE":
        return "FAILED-BASELINE"
    if status == "FAIL" and regression and any(regression.values()):
        return "FAILED-BASELINE"
    return "FAILED-NEW"  # FAIL, and FLAKY under the zero-flakiness policy


_FAIL_LINE = re.compile(r"^\s*(FAIL|not ok)\b")


def fail_lines(text, roots=()):
    """The failure lines of a log, with checkout and temporary paths normalised: with the failing
    step, the identity of a failure on two trees."""
    out = set()
    for line in text.splitlines():
        if _FAIL_LINE.match(line):
            for r in roots:
                line = line.replace(str(r), "<ROOT>")
            out.add(re.sub(r"/tmp/\S+", "<TMP>", line.strip()))
    return out


def recipe_lines(cwd, target, old=()):
    """C2: the commands `make -k target -o old...` would run, one per recipe line (continuations
    joined), from `make -n`."""
    out = subprocess.run(["make", "-n", target] + [x for p in old for x in ("-o", p)], cwd=cwd,
                         capture_output=True, text=True, env=clean_env()).stdout
    lines, cur = [], ""
    for line in out.splitlines():
        cur += line[:-1] + " " if line.endswith("\\") else line
        if not line.endswith("\\"):
            if cur.strip():
                lines.append(cur)
            cur = ""
    return lines


def run_lines(cwd, target, old, logdir, timeout, extra_env=None):
    """C2: a failing recipe stops at its first failing line, hiding the rest: run every line on its
    own (the target's prerequisites are built) and return [(command, rc)]."""
    results = []
    env = clean_env()
    env.update(extra_env or {})
    for i, cmd in enumerate(recipe_lines(cwd, target, old), 1):
        log = logdir / f"{target}.line{i}.log"
        with open(log, "w") as f:
            f.write(f"$ {cmd}\n")
            f.flush()
            try:
                rc = subprocess.run(["bash", "-c", cmd], cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=env,
                                    timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                rc = 124
        results.append((cmd[:200], rc))
    return results


def blocked_reason(text):
    for pat, why in HARNESS_BLOCKED:
        if pat.search(text):
            return why
    return None


def preflight_problems(f):
    """C3, pure (table-tested): the campaign's toolchain and oracle requirements."""
    out = []
    if "15.2" not in (f.get("cxx") or "") or "clang" in (f.get("cxx") or "").lower():
        out.append(f"GCC 15.2 required for comparator-sort certification (found: {f.get('cxx') or 'no compiler'})")
    if f.get("glibcxx") != 20260321:
        out.append(f"__GLIBCXX__ must be 20260321, the value the comparator-sort gate compiles for (found: {f.get('glibcxx')})")
    if f.get("comparator") != "ok":
        out.append(f"comparator sort is not compiled into the production binary ({f.get('comparator')})")
    if f.get("require_env") != "1":
        out.append("NIFT_CP14B_REQUIRE_COMPARATOR=1 is not set (comparator tests would SKIP instead of failing)")
    if not f.get("oracle"):
        out.append("no CP19 legacy oracle: pass --legacy-oracle (a v4.12.0 binary built from tag v4.12.0)")
    else:
        # The version string alone does not identify the oracle (Runtime 2.0 builds still report
        # v4.12.0): it must be the `nift` of a checkout of the frozen tag's commit, clean and
        # built from it (make -q nift).
        if f.get("oracle_version") != "v4.12.0":
            out.append(f"the CP19 legacy oracle {f.get('oracle')} is not v4.12.0 (reports: {f.get('oracle_version')})")
        if f.get("oracle_commit") != V4120_COMMIT:
            out.append(f"the CP19 legacy oracle is not built from v4.12.0 = {V4120_COMMIT[:12]} "
                       f"(its checkout is at: {f.get('oracle_commit')})")
        if f.get("oracle_clean") is not True or f.get("oracle_current") is not True:
            out.append("the CP19 legacy oracle's checkout has tracked changes or its nift is not built from it "
                       f"(clean: {f.get('oracle_clean')}, make -q nift: {f.get('oracle_current')})")
    return out


V4120_COMMIT = "ce8cd36a92ecfdc8b15b00df41a6c3bb9a31c0eb"  # tag v4.12.0
COMPARATOR_PROGRAM = "xs := [3, 1, 2]\nxs.sort((p, q) => p < q)\nprint(xs.stringify())\n"


def preflight_facts(root, oracle):
    cxx = os.environ.get("CXX", "g++")
    facts = {"require_env": os.environ.get("NIFT_CP14B_REQUIRE_COMPARATOR"), "oracle": oracle}
    try:
        facts["cxx"] = subprocess.run([cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    except (OSError, IndexError):
        facts["cxx"] = None
    try:
        out = subprocess.run([cxx, "-x", "c++", "-E", "-P", "-"], input="#include <cstddef>\n__GLIBCXX__\n",
                             capture_output=True, text=True).stdout.split()
        facts["glibcxx"] = int(out[-1]) if out and out[-1].isdigit() else None
    except (OSError, ValueError):
        facts["glibcxx"] = None
    nift = Path(root) / "nift"
    if not nift.exists():
        facts["comparator"] = "nift not built"
    else:
        work = Path(os.environ.get("TMPDIR", "/tmp")) / f"nift-certify-preflight-{os.getpid()}"
        work.mkdir(parents=True, exist_ok=True)
        (work / "c.f").write_text(COMPARATOR_PROGRAM)
        p = subprocess.run([str(nift), "c.f"], cwd=work, capture_output=True, text=True,
                           env=dict(clean_env(), NIFT_VM="require"))
        shutil.rmtree(work, ignore_errors=True)
        facts["comparator"] = "ok" if p.returncode == 0 and p.stdout.strip() == "[1,2,3]" else \
            f"a comparator sort exits {p.returncode}: {(p.stderr.strip().splitlines() or [''])[0][:120]}"
    if oracle:
        try:
            v = subprocess.run([oracle, "--version"], capture_output=True, text=True, env=dict(clean_env(), NIFT_VM="0"))
            m = re.search(r"v\d+\.\d+\.\d+", v.stdout + v.stderr)
            facts["oracle_version"] = m.group(0) if m else (v.stdout + v.stderr).strip()[:60]
        except OSError as e:
            facts["oracle_version"] = f"not runnable: {e}"
        tree = Path(oracle).resolve().parent
        git = lambda *a: subprocess.run(["git", "-C", str(tree)] + list(a), capture_output=True, text=True)
        head = git("rev-parse", "HEAD")
        facts["oracle_commit"] = head.stdout.strip() if head.returncode == 0 else "not a git checkout"
        st = git("status", "--porcelain", "--untracked-files=no")
        facts["oracle_clean"] = st.returncode == 0 and not st.stdout.strip()
        facts["oracle_current"] = (Path(oracle).resolve() == tree / "nift" and subprocess.run(
            ["make", "-q", "nift"], cwd=tree, capture_output=True, env=clean_env()).returncode == 0)
    return facts


def recipe_runs_make(text):
    return bool(re.search(r"\$\(MAKE\)|\$\{MAKE\}|(^|[;&|(\s])make\s", text))


def clean_env():
    env = dict(os.environ)
    # NIFT_DEFER_LOAD_SENSITIVE (CI only) would skip the load-sensitive
    # guards; certification always runs them (exclusively).
    for k in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES", "NIFT_DEFER_LOAD_SENSITIVE"):
        env.pop(k, None)
    return env


def run_target(cwd, target, log, timeout, user=None, old=(), extra_env=None):
    """Run one make target with a private TMPDIR (so tests that use fixed names
    under the temp directory, e.g. temp_directory_path()/"nift-host-seam-pg",
    cannot collide with concurrent targets, other checkouts or other users),
    in its own process group (killed on timeout)."""
    log.parent.mkdir(parents=True, exist_ok=True)
    tmp = Path(os.environ.get("TMPDIR", "/tmp")) / f"nift-certify-{os.getpid()}" / re.sub(r"[^\w.-]", "_", log.stem)
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    env = clean_env()
    env["TMPDIR"] = str(tmp)
    # The target's own log may sit inside the checkout (.build/certify). Tests
    # that snapshot the checkout exclude exactly this one file (and nothing else).
    env["NIFT_CERTIFY_TARGET_LOG"] = str(Path(log).resolve())
    env.update(extra_env or {})
    # C2: -k (a failing prerequisite stops nothing else); an umbrella's own recipe runs with its
    # gate prerequisites marked old (they are entries of their own).
    args = ["-k", target] + [x for p in old for x in ("-o", p)]
    cmd = ["make"] + args
    if user:
        shutil.chown(tmp, user=user)
        os.chmod(tmp.parent, 0o755)
        exports = " ".join(f"{k}='{v}'" for k, v in (extra_env or {}).items())
        cmd = ["su", user, "-s", "/bin/sh", "-c", f"cd '{cwd}' && TMPDIR='{tmp}' NIFT_CERTIFY_TARGET_LOG='{Path(log).resolve()}' {exports} make {' '.join(args)}"]
    t0 = time.time()
    with open(log, "w") as f:
        f.write(f"$ {' '.join(cmd)}  (cwd={cwd}, TMPDIR={tmp})\n")
        f.flush()
        p = subprocess.Popen(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=env,
                             start_new_session=True)
        try:
            rc = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.wait()
            f.write(f"\n[certify] TIMEOUT after {timeout}s\n")
            rc = 124
    shutil.rmtree(tmp, ignore_errors=True)
    return rc, round(time.time() - t0, 1)


# ----- environment-condition detectors (environment fact AND log evidence) -----

def detect_unverified(text):
    reasons = []
    for m in re.finditer(r"(?:^|\s|/)([A-Za-z0-9_.+-]+): (?:command )?not found", text, re.M):
        tool = m.group(1)
        if tool not in ("sh", "bash") and shutil.which(tool) is None:
            reasons.append(f"tool '{tool}' not installed")
    if re.search(r"go: download go\S*: .*(no such host|dial tcp|connection refused|403)", text):
        reasons.append("Go toolchain download blocked (no network access to proxy.golang.org)")
    for m in re.finditer(r"(/[\w./-]+): No such file or directory", text):
        path = m.group(1)
        if path.startswith(("/usr/bin/", "/bin/", "/usr/local/bin/")) and not os.path.exists(path):
            reasons.append(f"system tool {path} missing")
    for m in re.finditer(r"local package source is not a directory: (\S+)", text):
        if not os.path.isdir(m.group(1)):
            reasons.append(f"sibling checkout {m.group(1)} missing (CI checks out nift-packages)")
    return sorted(set(reasons))


def unprivileged_user(name):
    if os.geteuid() != 0 or not name:
        return None
    try:
        pwd.getpwnam(name)
        return name
    except KeyError:
        return None


def prepare_copy(src, dest, user):
    if dest.exists():
        shutil.rmtree(dest)
    subprocess.run(["cp", "-a", str(src), str(dest)], check=True)
    subprocess.run(["chown", "-R", f"{user}:", str(dest)], check=True)


SNAP_SKIP_DIRS = {".git", "__pycache__", "certify"}


def snapshot(root):
    state = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SNAP_SKIP_DIRS]
        for name in filenames:
            path = os.path.join(dirpath, name)
            try:
                st = os.lstat(path)
            except OSError:
                continue
            state[os.path.relpath(path, root)] = (st.st_size, st.st_mtime_ns)
    return state


def tmp_names():
    base = Path(os.environ.get("TMPDIR", "/tmp"))
    try:
        return {p.name for p in base.iterdir()}
    except OSError:
        return set()


def audit(planned, timeout, out_path):
    """Serial run recording, per target, every file it created/changed/removed
    in the checkout (including .build/) and every new top-level TMPDIR name it
    left behind. Shared outputs between targets and writes outside .build/
    identify targets that are not safe to run concurrently."""
    report = {}
    before = snapshot(ROOT)
    for t in planned:
        tmp0 = tmp_names()
        rc, dur = run_target(ROOT, t, ROOT / ".build" / "certify" / "audit" / f"{t}.log", timeout)
        after = snapshot(ROOT)
        changed = sorted(k for k in after if before.get(k) != after[k])
        removed = sorted(k for k in before if k not in after)
        report[t] = {"rc": rc, "seconds": dur, "changed": changed, "removed": removed,
                     "tmp_left": sorted(tmp_names() - tmp0)}
        print(f"[audit] {t}: rc={rc} changed={len(changed)} removed={len(removed)} tmp_left={len(report[t]['tmp_left'])}", flush=True)
        before = after
    writers = {}
    for t, r in report.items():
        for k in r["changed"] + r["removed"]:
            writers.setdefault(k, []).append(t)
    shared = {k: v for k, v in writers.items() if len(v) > 1}
    outside = {t: [k for k in r["changed"] + r["removed"] if not k.startswith(".build/")] for t, r in report.items()}
    out = {"targets": report, "shared_outputs": shared,
           "writes_outside_build": {t: v for t, v in outside.items() if v},
           "tmp_left": {t: r["tmp_left"] for t, r in report.items() if r["tmp_left"]}}
    Path(out_path).write_text(json.dumps(out, indent=2) + "\n")
    return out


_STEP = re.compile(r"(?:tests|scripts|tools)/[\w./-]+")


def failure_signature(text):
    """The step a target failed at: the last test/script path mentioned before
    make's final error line, or None if it cannot be identified. Used so that
    BASELINE-FAILURE is only claimed when the baseline fails at the same step
    (a baseline failure at an earlier step must not mask a later regression)."""
    lines = text.splitlines()
    end = max((i for i, l in enumerate(lines) if l.startswith("make: ***")), default=len(lines))
    for line in reversed(lines[:end]):
        m = _STEP.findall(line)
        if m:
            return m[-1]
    return None


# Wall-clock performance components inside a recipe (owner, 2026-10-23: the same
# patterns precert uses). make echoes each recipe line before running it, so a
# log that contains one of these reached a timing step.
PERF_COMPONENT = re.compile(r"benchmarks/[A-Za-z0-9_]+\.py|_perf_scaling_smoke\.sh|_scaling_guard\.sh|scripting_perf_guard\.sh")


def reached_timing(text):
    """True if the log shows any performance component was started."""
    return any(PERF_COMPONENT.search(l) for l in text.splitlines())


def decide(f):
    """The whole classification policy, as a pure function of observed facts.

    Order of evidence: pass; re-run alone; environment detected from the log;
    unprivileged re-run; baseline under the same user at the same failing step.
    Anything not positively shown to be acceptable is FAIL: a baseline failure
    never excuses a candidate failure at a different or unidentified step.
    """
    t = f["target"]
    if f["rc"] == 0:
        return "PASS", ""
    if f["load_sensitive"]:
        # Already ran with no competing target: a later pass does not resolve
        # the failure (owner review, D-012). Thresholds unchanged.
        # Root-only exception (owner, 2026-10-23, CP16): only when the failure is
        # shown to be privilege-related and before any timing step: both root runs
        # failed at the same identified step, neither root log reached a
        # performance component, and the complete target (every timing step
        # included) then passed as the unprivileged user. Anything else, including
        # missing or incomplete unprivileged evidence, stays FAIL.
        steps = f.get("root_steps") or (None, None)
        if (f["rerun_rc"] != 0 and steps[0] is not None and steps[0] == steps[1]
                and f.get("root_reached_timing") is False and f.get("unpriv_rc") == 0):
            return "SKIPPED-ENVIRONMENTAL", (f"fails only as root at functional step {steps[0]} (before any timing "
                                             f"step, both root runs); complete target incl. timing passes as "
                                             f"unprivileged user '{f['user']}'")
        return "FAIL", (f"load-sensitive guard failed under exclusive execution "
                        f"(re-run alone: {'passed' if f['rerun_rc'] == 0 else 'failed'}); see .build/certify/{t}.log")
    if f["rerun_rc"] == 0:
        return "FLAKY", "failed in the concurrent run, passed when re-run alone"
    if f.get("unverified"):
        return "UNVERIFIED", "; ".join(f["unverified"])
    if f.get("unpriv_rc") == 0:
        return "SKIPPED-ENVIRONMENTAL", f"fails only as root; passes as unprivileged user '{f['user']}'"
    if f.get("base_rc") not in (None, 0) and f.get("base_has_target"):
        cs, bs = f.get("cand_step"), f.get("base_step")
        if cs is not None and cs == bs:
            return "BASELINE-FAILURE", f"also fails on the v4.12.0 baseline at the same step, same user: {cs}"
        return "FAIL", (f"baseline also fails, but at a different or unidentified step "
                        f"(candidate: {cs}; baseline: {bs}); see {f.get('cand_log')}")
    return "FAIL", f"see .build/certify/{t}.log"


def ensure_baseline(path, jobs):
    if not (path / "Makefile").exists():
        return False
    if not (path / "nift").exists():
        built = subprocess.run(["make", f"-j{jobs}", "nift"], cwd=path, capture_output=True, env=clean_env())
        if built.returncode != 0:
            print(f"certify: baseline build failed in {path}; BASELINE-FAILURE cannot be classified", file=sys.stderr)
            return False
    return (path / "nift").exists()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jobs", "-j", type=int, default=max(1, (os.cpu_count() or 2)))
    ap.add_argument("--targets", nargs="+", help="focused run (tier 1/2): only these targets (umbrellas expanded)")
    ap.add_argument("--quick", action="store_true", help="implementation-time tier: the QUICK smoke set")
    ap.add_argument("--timeout", type=int, default=3600, help="per-target timeout in seconds")
    ap.add_argument("--unprivileged-user", default=os.environ.get("NIFT_CERTIFY_USER", "tester"),
                    help="when running as root, re-run failures as this user to detect root-only failures")
    ap.add_argument("--baseline", default=os.environ.get("NIFT_CERTIFY_BASELINE",
                                                         str(ROOT.parent / "baseline-src")),
                    help="v4.12.0 source tree used to classify BASELINE-FAILURE ('' disables)")
    ap.add_argument("--regression-baseline", action="append", default=[], metavar="NAME=TREE",
                    help="a regression baseline tree (repeatable): the accepted B4b 17f7964c, weekend/r2")
    ap.add_argument("--legacy-oracle", default=os.environ.get("CP19_LEGACY_ORACLE", ""),
                    help="the v4.12.0 legacy binary test-cp19-proof-cli compares with (CP19_LEGACY_ORACLE)")
    ap.add_argument("--strict", action="store_true", help="exit 1 unless every required gate is PASSED")
    ap.add_argument("--list", action="store_true", help="dry run: print the plan, the inventory check and the preflight, then exit")
    ap.add_argument("--check-plan", action="store_true", help="exit 1 when the plan misses an inventory or mandatory gate")
    ap.add_argument("--drop", action="append", default=[], metavar="TARGET",
                    help="remove TARGET from the plan before --check-plan (negative control)")
    ap.add_argument("--audit", metavar="JSON", help="isolation audit: run targets serially and record writes (use a disposable copy)")
    a = ap.parse_args()

    deps, recipe = make_db(ROOT, AGGREGATE)
    inv = inventory(deps)
    roots = a.targets or [AGGREGATE] + [t for t in inv if t not in NON_GATE]
    entries, covered = plan(deps, recipe, roots)
    if a.quick:
        entries = [e for e in entries if e[0] in QUICK]
    for t in a.drop:
        entries = [e for e in entries if e[0] != t]
        covered.discard(t)
    problems = inventory_problems({e[0] for e in entries} | {t for t in covered if not recipe.get(t)},
                                  inv, NON_GATE, MANDATORY, set(deps))
    if a.check_plan or (a.list and not a.targets):
        for pr in problems:
            print("FAIL certify plan: " + pr)
        if a.check_plan:
            if not problems:
                print(f"PASS certify plan: {len(entries)} entries; every one of the {len(inv)} inventory targets is "
                      f"planned or one of {len(NON_GATE)} named non-gates; {len(MANDATORY)} mandatory gates planned")
            return 1 if problems else 0
    planned = [e[0] for e in entries]
    olds = {e[0]: e[2] if e[1] == "own" else () for e in entries}
    # Entries that are not certify-targets leaves of their own (umbrella prerequisites, inventory
    # additions, own recipes) run serially, in plan order: before C2 they ran serially inside
    # their umbrella or not at all, and their isolation is not audited.
    old_leaves = set(leaves(deps, recipe, AGGREGATE))
    exclusive = {}
    for t, kind, _ in entries:
        if t in EXCLUSIVE or recipe_runs_make(recipe.get(t, "")):
            exclusive[t] = EXCLUSIVE.get(t) or "recipe runs a sub-make (shared build outputs)"
        elif kind == "own":
            exclusive[t] = "an umbrella's own recipe (its gate prerequisites are entries of their own)"
        elif t not in old_leaves:
            exclusive[t] = "planned by the C1 inventory or expanded from an umbrella (isolation not audited)"
    # own recipes last, in plan order
    exclusive = dict(sorted(exclusive.items(), key=lambda kv: (olds.get(kv[0]) != (), planned.index(kv[0]))))
    parallel = [t for t in planned if t not in exclusive]
    oracle = a.legacy_oracle or None
    full = not a.targets and not a.quick
    if a.list:
        for t in parallel:
            print(f"parallel   {t}")
        for t, why in exclusive.items():
            print(f"exclusive  {t}  ({why})" + (f"  [make -k {t} " + " ".join("-o " + o for o in olds[t]) + "]" if olds[t] else ""))
        pf = preflight_problems(preflight_facts(ROOT, oracle))
        print(f"plan: {len(planned)} required gates ({len(parallel)} parallel, {len(exclusive)} serial), "
              f"{len(inv)} inventory targets, {len(NON_GATE)} non-gates: " + ", ".join(sorted(NON_GATE)))
        for pr in pf:
            print("PREFLIGHT BLOCKED: " + pr)
        print("preflight: " + ("ok" if not pf else f"{len(pf)} problem(s): a full campaign would exit 2 here"))
        return 1 if problems and not a.targets else 0
    if not planned:
        print("certify: no targets planned", file=sys.stderr)
        return 2
    if problems and full:
        for pr in problems:
            print("certify: plan incomplete: " + pr, file=sys.stderr)
        return 2

    logdir = ROOT / ".build" / "certify"
    logdir.mkdir(parents=True, exist_ok=True)
    if a.audit:
        built = subprocess.run(["make", f"-j{a.jobs}", "nift"], cwd=ROOT, capture_output=True, env=clean_env())
        if built.returncode != 0:
            print("certify: could not build nift for the audit", file=sys.stderr)
            return 1
        audit(planned, a.timeout, a.audit)
        return 0
    # C3 preflight, before anything else is built: the production binary (for the comparator
    # check), then the toolchain, switch and oracle facts. A full campaign does not start with a
    # preflight problem.
    built = subprocess.run(["make", f"-j{a.jobs}", "nift"], cwd=ROOT, capture_output=True, text=True, env=clean_env())
    (logdir / "_build-nift.log").write_text(built.stdout + built.stderr)
    facts = preflight_facts(ROOT, oracle)
    pf = preflight_problems(facts)
    (logdir / "_preflight.json").write_text(json.dumps({"facts": facts, "problems": pf}, indent=2) + "\n")
    for pr in pf:
        print("PREFLIGHT BLOCKED: " + pr, file=sys.stderr)
    if pf and full:
        print("certify: preflight failed; the campaign did not start (.build/certify/_preflight.json)", file=sys.stderr)
        return 2
    # Phase 0: build every non-test prerequisite once (binaries, objects,
    # staged prefixes, bindings), so concurrent make processes do not race to
    # build shared outputs. -k: a prerequisite that cannot be built here (e.g. a
    # missing toolchain) is left for its targets to fail and be classified.
    prereqs = sorted({d for t in planned for d in deps.get(t, []) if not TEST_NAME.match(d)} | {"nift"})
    pre = subprocess.run(["make", "-k", f"-j{a.jobs}"] + prereqs, cwd=ROOT, capture_output=True, text=True, env=clean_env())
    (logdir / "_prebuild.log").write_text(pre.stdout + pre.stderr)
    if not (ROOT / "nift").exists():
        print("certify: could not build nift, see .build/certify/_prebuild.log", file=sys.stderr)
        return 1
    extra = {"CP19_LEGACY_ORACLE": oracle} if oracle else {}

    results = {}
    started = time.time()
    with cf.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        futs = {pool.submit(run_target, ROOT, t, logdir / f"{t}.log", a.timeout, None, olds[t], extra): t for t in parallel}
        for fut in cf.as_completed(futs):
            t = futs[fut]
            rc, dur = fut.result()
            results[t] = {"rc": rc, "seconds": dur, "mode": "parallel"}
            print(f"[{'ok' if rc == 0 else 'FAILED'}] {t} ({dur}s)", flush=True)
    for t in exclusive:
        rc, dur = run_target(ROOT, t, logdir / f"{t}.log", a.timeout, None, olds[t], extra)
        results[t] = {"rc": rc, "seconds": dur, "mode": "exclusive"}
        print(f"[{'ok' if rc == 0 else 'FAILED'}] {t} ({dur}s, exclusive)", flush=True)

    # Classification of failures: rerun alone, then environment, then baseline.
    user = unprivileged_user(a.unprivileged_user)
    user_copy = None
    baseline = Path(a.baseline) if a.baseline else None
    baseline_ready = None
    baseline_user_copy = None
    regressions = {}
    for spec in a.regression_baseline:
        name, _, tree = spec.partition("=")
        if name and tree and ensure_baseline(Path(tree), a.jobs):
            regressions[name] = Path(tree)
        else:
            print(f"certify: regression baseline {spec!r} unavailable; its comparison is not made", file=sys.stderr)
    comparator_skipped = []
    for t, r in sorted(results.items()):
        # Gather facts; the verdict comes only from decide(), which is pure and
        # table-tested (tests/runtime2/certification_guard.sh).
        f = {"target": t, "rc": r["rc"], "load_sensitive": t in LOAD_SENSITIVE or t in PERF_LANE, "user": user}
        if r["rc"] != 0:
            f["rerun_rc"], _ = run_target(ROOT, t, logdir / f"{t}.rerun.log", a.timeout, None, olds[t], extra)
        if r["rc"] != 0 and f["rerun_rc"] != 0 and f["load_sensitive"] and os.geteuid() == 0 and user:
            # Root evidence is kept as is ({t}.log, {t}.rerun.log); the unprivileged
            # run is the complete target, timing steps included.
            texts = [(logdir / f"{t}.log").read_text(errors="replace"), (logdir / f"{t}.rerun.log").read_text(errors="replace")]
            f["root_steps"] = tuple(failure_signature(x) for x in texts)
            f["root_reached_timing"] = any(reached_timing(x) for x in texts)
            if f["root_steps"][0] is not None and f["root_steps"][0] == f["root_steps"][1] and not f["root_reached_timing"]:
                if user_copy is None:
                    user_copy = Path(pwd.getpwnam(user).pw_dir) / "nift-certify-copy"
                    prepare_copy(ROOT, user_copy, user)
                f["unpriv_rc"], _ = run_target(user_copy, t, logdir / f"{t}.unprivileged.log", a.timeout, user, olds[t], extra)
        if r["rc"] != 0 and f["rerun_rc"] != 0 and not f["load_sensitive"]:
            text = (logdir / f"{t}.log").read_text(errors="replace") + (logdir / f"{t}.rerun.log").read_text(errors="replace")
            f["unverified"] = detect_unverified(text)
            cand_log, base_user = logdir / f"{t}.log", None
            if not f["unverified"] and os.geteuid() == 0 and user:
                if user_copy is None:
                    user_copy = Path(pwd.getpwnam(user).pw_dir) / "nift-certify-copy"
                    prepare_copy(ROOT, user_copy, user)
                f["unpriv_rc"], _ = run_target(user_copy, t, logdir / f"{t}.unprivileged.log", a.timeout, user, olds[t], extra)
                # The unprivileged run gets past root-only steps and may fail later:
                # it is the candidate result the baseline must be compared with.
                cand_log, base_user = logdir / f"{t}.unprivileged.log", user
            if not f["unverified"] and f.get("unpriv_rc") != 0 and baseline:
                if baseline_ready is None:
                    baseline_ready = ensure_baseline(baseline, a.jobs)
                if baseline_ready:
                    base_dir = baseline   # same user as the candidate log it is compared with
                    if base_user:
                        if baseline_user_copy is None:
                            baseline_user_copy = Path(pwd.getpwnam(base_user).pw_dir) / "nift-certify-baseline-copy"
                            prepare_copy(baseline, baseline_user_copy, base_user)
                        base_dir = baseline_user_copy
                    blog = logdir / f"{t}.baseline.log"
                    f["base_rc"], _ = run_target(base_dir, t, blog, a.timeout, base_user, olds[t], extra)
                    btext = blog.read_text(errors="replace")
                    f["base_has_target"] = "No rule to make target" not in btext
                    f["cand_step"] = failure_signature(cand_log.read_text(errors="replace"))
                    f["base_step"] = failure_signature(btext)
                    f["cand_log"] = str(cand_log.relative_to(ROOT))
            # C2: the regression baselines, each compared on its own (same step AND same FAIL lines).
            if not f["unverified"] and f.get("unpriv_rc") != 0 and regressions:
                ctext = cand_log.read_text(errors="replace")
                r["regression"] = {}
                for name, tree in regressions.items():
                    rlog = logdir / f"{t}.regression-{re.sub(r'[^\w.-]', '_', name)}.log"
                    rrc, _ = run_target(tree, t, rlog, a.timeout, None, olds[t], extra)
                    rtext = rlog.read_text(errors="replace")
                    same = (rrc != 0 and "No rule to make target" not in rtext
                            and failure_signature(ctext) is not None and failure_signature(ctext) == failure_signature(rtext)
                            and fail_lines(ctext, [ROOT]) == fail_lines(rtext, [tree]))
                    r["regression"][name] = same
        r["status"], r["reason"] = decide(f)
        if r["rc"] != 0 and recipe.get(t, "").count("\n") > 1:
            # C2: the lines after the failing one would never run: each line on its own.
            r["lines"] = run_lines(ROOT, t, olds[t], logdir, a.timeout, extra)
            r["reason"] += "; recipe lines failing on their own: " + (", ".join(
                f"{i}" for i, (_, lrc) in enumerate(r["lines"], 1) if lrc != 0) or "none")
        logtext = (logdir / f"{t}.log").read_text(errors="replace") if (logdir / f"{t}.log").exists() else ""
        r["blocked"] = blocked_reason(logtext)
        if COMPARATOR_SKIP in logtext:
            comparator_skipped.append(t)
        r["class"] = report_class(r["status"], r.get("regression"), r["blocked"])
        if r["class"] == "BLOCKED":
            r["reason"] = r["blocked"] or r["reason"]
        elif r["class"] == "FAILED-BASELINE" and r["status"] == "FAIL":
            r["reason"] = "identical failure on regression baseline(s): " + ", ".join(
                n for n, same in r["regression"].items() if same)
    for t in planned:
        if t not in results:
            results[t] = {"rc": None, "status": None, "class": "NOT-RUN", "reason": "planned, not run"}

    classes = ["FAILED-NEW", "FAILED-BASELINE", "BLOCKED", "NOT-RUN", "PASSED"]
    counts = {k: sum(1 for r in results.values() if r["class"] == k) for k in classes}
    order = ["FAIL", "BASELINE-FAILURE", "UNVERIFIED", "SKIPPED-ENVIRONMENTAL", "FLAKY", "PASS"]
    detail = {k: sum(1 for r in results.values() if r.get("status") == k) for k in order}
    summary = {
        "root": str(ROOT), "commit": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip(),
        "euid": os.geteuid(), "jobs": a.jobs, "seconds": round(time.time() - started, 1),
        "counts": counts, "decide_counts": detail, "exclusive": exclusive, "preflight": {"facts": facts, "problems": pf},
        "baselines": {"v4.12.0 (legacy compatibility)": str(baseline) if baseline else None,
                      **{f"{n} (regression)": str(p) for n, p in regressions.items()},
                      "approved differences": "docs/runtime-2/evidence/b5/expected-changes.tsv (checked by the gates)"},
        "comparator_skipped": comparator_skipped,
        "targets": {t: results[t] for t in sorted(results)},
    }
    (logdir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    lines = [f"certify @ {summary['commit'][:7]}: " + ", ".join(f"{k} {v}" for k, v in counts.items())]
    for k in classes[:-1]:
        for t in sorted(t for t, r in results.items() if r["class"] == k):
            lines.append(f"  {k:<16} {t}: {results[t]['reason']}")
    if comparator_skipped:
        lines.append("CAMPAIGN FAILED: comparator coverage skipped in " + ", ".join(comparator_skipped))
    not_passed = sum(counts[k] for k in classes[:-1])
    if not_passed:
        lines.append(f"NOT CERTIFIED HERE: {not_passed} required gate(s) are not PASSED (only PASSED counts as a pass)")
    (logdir / "summary.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    if counts["FAILED-NEW"] or comparator_skipped:
        return 1
    if counts["BLOCKED"] or counts["NOT-RUN"]:
        return 3
    if a.strict and counts["FAILED-BASELINE"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
