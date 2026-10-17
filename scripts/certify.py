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
  BASELINE-FAILURE      fails identically on the v4.12.0 baseline tree under the
                        same conditions (pre-existing, not a Runtime 2.0 regression)
  FAIL                  anything else

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
    "test-build-boundary-nondestructive": "snapshots the whole checkout before/after; any concurrent writer is reported as a modification",
    "test-build-boundary": "builds from a clean copy of the checkout; reads the tree while others write it",
    "bh2-test-integrity": "aggregate evidence gate that re-runs other suites",
    "test-bindings": "stages dist/embed-prefix and binding build trees shared with the binding targets",
    "test-embed": "stages dist/embed-prefix shared with the binding targets",
}

# Implementation-time tier (--quick): fast, broad smoke of the runtime and the
# Runtime 2.0 gates. Checkpoint certification always runs the full set.
QUICK = [
    "test-runtime2-certification-guard", "test-runtime2-vm-corpus", "test-runtime2-modified-race",
    "test-runtime2-dynamic-reads", "test-runtime2-fs-freedom", "test-conformance", "test-contracts",
    "test-requirements", "test-dependency-status", "test-call-semantics", "test-control-flow",
]

TEST_NAME = re.compile(r"^(test|bh)[A-Za-z0-9_.-]*$")


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
    for k in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES"):
        env.pop(k, None)
    return env


def run_target(cwd, target, log, timeout, user=None):
    log.parent.mkdir(parents=True, exist_ok=True)
    cmd = ["make", target]
    if user:
        cmd = ["su", user, "-s", "/bin/sh", "-c", f"cd '{cwd}' && make {target}"]
    t0 = time.time()
    with open(log, "w") as f:
        f.write(f"$ {' '.join(cmd)}  (cwd={cwd})\n")
        f.flush()
        p = subprocess.Popen(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=clean_env(),
                             start_new_session=True)
        try:
            rc = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.wait()
            f.write(f"\n[certify] TIMEOUT after {timeout}s\n")
            rc = 124
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


def ensure_baseline(path, jobs):
    if not (path / "Makefile").exists():
        return False
    if not (path / "nift").exists():
        subprocess.run(["make", f"-j{jobs}", "nift"], cwd=path, capture_output=True, env=clean_env())
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
        subprocess.run(["make", "-k", f"-j{a.jobs}", "nift"], cwd=ROOT, capture_output=True, env=clean_env())
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
    for t, r in sorted(results.items()):
        if r["rc"] == 0:
            r["status"], r["reason"] = "PASS", ""
            continue
        rc, _ = run_target(ROOT, t, logdir / f"{t}.rerun.log", a.timeout)
        if rc == 0:
            r["status"], r["reason"] = "FLAKY", "failed in the concurrent run, passed when re-run alone"
            continue
        text = (logdir / f"{t}.log").read_text(errors="replace") + (logdir / f"{t}.rerun.log").read_text(errors="replace")
        unverified = detect_unverified(text)
        if unverified:
            r["status"], r["reason"] = "UNVERIFIED", "; ".join(unverified)
            continue
        if os.geteuid() == 0 and user:
            if user_copy is None:
                user_copy = Path(pwd.getpwnam(user).pw_dir) / "nift-certify-copy"
                prepare_copy(ROOT, user_copy, user)
            urc, _ = run_target(user_copy, t, logdir / f"{t}.unprivileged.log", a.timeout, user=user)
            if urc == 0:
                r["status"], r["reason"] = "SKIPPED-ENVIRONMENTAL", f"fails only as root; passes as unprivileged user '{user}'"
                continue
        if baseline:
            if baseline_ready is None:
                baseline_ready = ensure_baseline(baseline, a.jobs)
            if baseline_ready:
                brc, _ = run_target(baseline, t, logdir / f"{t}.baseline.log", a.timeout)
                btext = (logdir / f"{t}.baseline.log").read_text(errors="replace")
                if brc != 0 and "No rule to make target" not in btext:
                    r["status"], r["reason"] = "BASELINE-FAILURE", f"also fails on the v4.12.0 baseline ({baseline})"
                    continue
        r["status"], r["reason"] = "FAIL", f"see .build/certify/{t}.log"

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
