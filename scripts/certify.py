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

Only PASS counts as a pass. Exit status is 1 when any target is FAIL (or when a
--strict run has anything but PASS/FLAKY), else 0. Logs: .build/certify/<target>.log;
summary: .build/certify/summary.json and summary.txt.
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


def recipe_runs_make(text):
    return bool(re.search(r"\$\(MAKE\)|\$\{MAKE\}|(^|[;&|(\s])make\s", text))


def clean_env():
    env = dict(os.environ)
    # NIFT_DEFER_LOAD_SENSITIVE (CI only) would skip the load-sensitive
    # guards; certification always runs them (exclusively).
    for k in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES", "NIFT_DEFER_LOAD_SENSITIVE"):
        env.pop(k, None)
    return env


def run_target(cwd, target, log, timeout, user=None):
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
    cmd = ["make", target]
    if user:
        shutil.chown(tmp, user=user)
        os.chmod(tmp.parent, 0o755)
        cmd = ["su", user, "-s", "/bin/sh", "-c", f"cd '{cwd}' && TMPDIR='{tmp}' NIFT_CERTIFY_TARGET_LOG='{Path(log).resolve()}' make {target}"]
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
    ap.add_argument("--targets", nargs="+", help="focused run (tier 1/2): only these targets")
    ap.add_argument("--quick", action="store_true", help="implementation-time tier: the QUICK smoke set")
    ap.add_argument("--timeout", type=int, default=3600, help="per-target timeout in seconds")
    ap.add_argument("--unprivileged-user", default=os.environ.get("NIFT_CERTIFY_USER", "tester"),
                    help="when running as root, re-run failures as this user to detect root-only failures")
    ap.add_argument("--baseline", default=os.environ.get("NIFT_CERTIFY_BASELINE",
                                                         str(ROOT.parent / "baseline-src")),
                    help="v4.12.0 source tree used to classify BASELINE-FAILURE ('' disables)")
    ap.add_argument("--strict", action="store_true", help="exit 1 unless every target is PASS or FLAKY")
    ap.add_argument("--list", action="store_true", help="print the expanded target plan and exit")
    ap.add_argument("--audit", metavar="JSON", help="isolation audit: run targets serially and record writes (use a disposable copy)")
    a = ap.parse_args()

    deps, recipe = make_db(ROOT, AGGREGATE)
    planned = leaves(deps, recipe, AGGREGATE)
    if a.targets:
        planned = a.targets
    elif a.quick:
        planned = [t for t in QUICK if t in planned]
    exclusive = {t: EXCLUSIVE.get(t) or "recipe runs a sub-make (shared build outputs)"
                 for t in planned if t in EXCLUSIVE or recipe_runs_make(recipe.get(t, ""))}
    parallel = [t for t in planned if t not in exclusive]
    if a.list:
        for t in parallel:
            print(f"parallel   {t}")
        for t, why in exclusive.items():
            print(f"exclusive  {t}  ({why})")
        return 0
    if not planned:
        print("certify: no targets planned", file=sys.stderr)
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

    results = {}
    started = time.time()
    with cf.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        futs = {pool.submit(run_target, ROOT, t, logdir / f"{t}.log", a.timeout): t for t in parallel}
        for fut in cf.as_completed(futs):
            t = futs[fut]
            rc, dur = fut.result()
            results[t] = {"rc": rc, "seconds": dur, "mode": "parallel"}
            print(f"[{'ok' if rc == 0 else 'FAILED'}] {t} ({dur}s)", flush=True)
    for t in exclusive:
        rc, dur = run_target(ROOT, t, logdir / f"{t}.log", a.timeout)
        results[t] = {"rc": rc, "seconds": dur, "mode": "exclusive"}
        print(f"[{'ok' if rc == 0 else 'FAILED'}] {t} ({dur}s, exclusive)", flush=True)

    # Classification of failures: rerun alone, then environment, then baseline.
    user = unprivileged_user(a.unprivileged_user)
    user_copy = None
    baseline = Path(a.baseline) if a.baseline else None
    baseline_ready = None
    baseline_user_copy = None
    for t, r in sorted(results.items()):
        # Gather facts; the verdict comes only from decide(), which is pure and
        # table-tested (tests/runtime2/certification_guard.sh).
        f = {"target": t, "rc": r["rc"], "load_sensitive": t in LOAD_SENSITIVE, "user": user}
        if r["rc"] != 0:
            f["rerun_rc"], _ = run_target(ROOT, t, logdir / f"{t}.rerun.log", a.timeout)
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
                f["unpriv_rc"], _ = run_target(user_copy, t, logdir / f"{t}.unprivileged.log", a.timeout, user=user)
        if r["rc"] != 0 and f["rerun_rc"] != 0 and not f["load_sensitive"]:
            text = (logdir / f"{t}.log").read_text(errors="replace") + (logdir / f"{t}.rerun.log").read_text(errors="replace")
            f["unverified"] = detect_unverified(text)
            cand_log, base_user = logdir / f"{t}.log", None
            if not f["unverified"] and os.geteuid() == 0 and user:
                if user_copy is None:
                    user_copy = Path(pwd.getpwnam(user).pw_dir) / "nift-certify-copy"
                    prepare_copy(ROOT, user_copy, user)
                f["unpriv_rc"], _ = run_target(user_copy, t, logdir / f"{t}.unprivileged.log", a.timeout, user=user)
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
                    f["base_rc"], _ = run_target(base_dir, t, blog, a.timeout, user=base_user)
                    btext = blog.read_text(errors="replace")
                    f["base_has_target"] = "No rule to make target" not in btext
                    f["cand_step"] = failure_signature(cand_log.read_text(errors="replace"))
                    f["base_step"] = failure_signature(btext)
                    f["cand_log"] = str(cand_log.relative_to(ROOT))
        r["status"], r["reason"] = decide(f)

    order = ["FAIL", "BASELINE-FAILURE", "UNVERIFIED", "SKIPPED-ENVIRONMENTAL", "FLAKY", "PASS"]
    counts = {k: sum(1 for r in results.values() if r["status"] == k) for k in order}
    summary = {
        "root": str(ROOT), "commit": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip(),
        "euid": os.geteuid(), "jobs": a.jobs, "seconds": round(time.time() - started, 1),
        "counts": counts, "exclusive": exclusive,
        "targets": {t: results[t] for t in sorted(results)},
    }
    (logdir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    lines = [f"certify @ {summary['commit'][:7]}: " + ", ".join(f"{k} {v}" for k, v in counts.items())]
    for k in order[:-1]:
        for t in sorted(t for t, r in results.items() if r["status"] == k):
            lines.append(f"  {k:<22} {t}: {results[t]['reason']}")
    not_passed = sum(counts[k] for k in order[1:5])
    if not_passed:
        lines.append(f"NOT FULLY VERIFIED HERE: {not_passed} target(s) are not counted as passes (see reasons; CI covers UNVERIFIED)")
    (logdir / "summary.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    if counts["FAIL"]:
        return 1
    if a.strict and any(counts[k] for k in ("BASELINE-FAILURE", "UNVERIFIED", "SKIPPED-ENVIRONMENTAL")):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
