#!/usr/bin/env python3
"""Snap publication coordinator for the nift snap (manual, best-effort).

The Snapcraft/Launchpad build service connected to the GitHub repository reads
snap/snapcraft.yaml and publishes repository builds for the declared platforms
to latest/edge. This coordinator is a **manual** maintenance utility and the
only publisher: it is invoked by the maintainer (via the `Promote completed
Snap builds` workflow or directly) only after the builders have been inspected
and are believed ready. It releases exactly the selected edge revisions to
latest/candidate, verifies the candidate set, runs the amd64 candidate
confinement smoke, then releases each selected revision explicitly to
latest/stable (never whole-channel `snapcraft promote`), and verifies stable.

Required coordinated targets (a Nift GitHub release never waits for these or
for Snap): `amd64`, `arm64`, `armhf`, `ppc64el`, `s390x`.

Best-effort target: `riscv64`. It must never delay or fail a promotion: it is
included only when its edge build is already at the target version, it may
remain on an older stable version while builders are unavailable, it may skip
intermediate versions, and a delayed older build must never downgrade a newer
stable RISC-V revision.

Whole-channel `snapcraft promote` is deliberately not used: its completeness
policy requires the entire set of ever-released store architectures, which
includes the historical i386 entry that Nift no longer declares or builds.
Per-revision releases are idempotent: a rerun preserves already-correct stable
assignments and resumes a partial publication safely.

Legacy channel entries outside the declared platform set (e.g. i386 at an old
version) are ignored and reported in edge and stable, and never released,
promoted, replaced or closed. In candidate they are a fail-closed condition,
because the strict candidate set is the exact source for the stable releases.

The decision logic is pure and importable for offline tests; running this module
executes the real Store transaction (unless --dry-run is given).

Modes:
  --status (default)     read-only report of per-architecture edge/candidate/
                         stable state and required/best-effort readiness.
  --check-ready          read-only preflight: fail if a required architecture
                         is not yet at the version on latest/edge.
  --promote-candidate    release the ready edge revisions to latest/candidate.
  --promote-stable       release the verified candidate revisions to
                         latest/stable.
  --dry-run              print snapcraft commands instead of executing them.

No mode performs an initial long poll for remote builds: the maintainer only
invokes promotion after inspecting the build page, so a missing required build
fails fast. Short bounded waits (default ~5 minutes each) remain only after a
deliberate channel mutation (candidate/stable convergence).

Environment:
  SNAP_NAME                   snap name (default: nift)
  NIFT_SNAP_VERSION           release version to coordinate (no leading 'v')
  NIFT_SNAP_CONFIRM           must be "yes" for any mutation mode
  NIFT_SNAP_CANDIDATE_WAIT    seconds to wait for candidate convergence
                              (default 300)
  NIFT_SNAP_STABLE_WAIT       seconds to wait for stable convergence
                              (default 300)
  NIFT_SNAP_POLL              poll interval (default 20)
  SNAPCRAFT_STORE_CREDENTIALS required for any release operation
"""

import datetime
import json
import os
import re
import subprocess
import sys
import time
import urllib.request

SNAP_NAME = os.environ.get("SNAP_NAME", "nift")
STORE_API = "https://api.snapcraft.io/v2/snaps/info"
# Documented contract for the complete supported Snap architecture set. The
# authoritative source is snap/snapcraft.yaml (platforms:); this constant is the
# documented invariant the offline contract test pins the file to.
REQUIRED_ARCHS = {"amd64", "arm64", "armhf", "ppc64el", "s390x"}
BEST_EFFORT_ARCHS = {"riscv64"}
EXPECTED_ARCHS = REQUIRED_ARCHS | BEST_EFFORT_ARCHS


def parse_platforms(yaml_text):
    """Minimal, dependency-free parse of the top-level `platforms:` block."""
    archs = set()
    in_platforms = False
    for raw in yaml_text.splitlines():
        line = raw.rstrip()
        if not in_platforms:
            if line == "platforms:":
                in_platforms = True
            continue
        if line and not line[0].isspace():
            break  # next top-level key ends the platforms block
        m = re.fullmatch(r"(\s+)([a-z0-9_-]+):\s*", line)
        if m:
            archs.add(m.group(2))
    return archs


def load_platforms(path="snap/snapcraft.yaml"):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return parse_platforms(f.read())
    except OSError:
        return set()


def parse_channel_map_response(payload):
    """Validate a raw API payload and return its channel-map list. Raises on an
    API error-list so callers fail closed instead of silently selecting nothing."""
    if isinstance(payload, dict) and "error-list" in payload:
        raise RuntimeError("Snap Store API error: " + json.dumps(payload["error-list"]))
    return payload.get("channel-map", []) if isinstance(payload, dict) else []


def fetch_channel_map(snap_name=SNAP_NAME):
    # The parser needs the top-level revision and version, so both fields must
    # be requested explicitly; fields=channel-map alone omits them.
    url = "{}/{}?fields=channel-map,revision,version".format(STORE_API, snap_name)
    # The v2 info endpoint requires the Snap-Device-Series header.
    req = urllib.request.Request(
        url, headers={"Snap-Device-Series": "16", "User-Agent": "nift-release-coordinator/1"}
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        payload = json.load(resp)
    return parse_channel_map_response(payload)


def entry_problem(entry):
    """Validate one channel-map entry. Returns a problem string or None.

    Every selected entry must carry a non-empty architecture, channel
    track/risk, a positive integer revision and a non-empty version. Entries
    returned by fields=channel-map alone lack revision/version and are
    therefore malformed rather than "still building".
    """
    ch = entry.get("channel") or {}
    arch = ch.get("architecture")
    if not isinstance(arch, str) or not arch:
        return "entry missing a non-empty channel architecture"
    if not isinstance(ch.get("track"), str) or not ch.get("track"):
        return "entry for {} missing channel track".format(arch)
    if not isinstance(ch.get("risk"), str) or not ch.get("risk"):
        return "entry for {} missing channel risk".format(arch)
    revision = entry.get("revision")
    if not isinstance(revision, int) or revision <= 0:
        return "entry for {} revision is not a positive integer: {!r}".format(arch, revision)
    version = entry.get("version")
    if not isinstance(version, str) or not version:
        return "entry for {} version is not a non-empty string: {!r}".format(arch, version)
    return None


def channel_entries(channel_map, track, risk):
    """(architecture, revision, version) for unbranched track/risk entries.

    Matches the real v2 channel-map schema: architecture lives inside `channel`,
    revision/version are top-level, and unbranched responses omit `branch`.
    """
    out = []
    for entry in channel_map:
        ch = entry.get("channel") or {}
        branch = ch.get("branch")
        if ch.get("track") == track and ch.get("risk") == risk and branch in (None, ""):
            out.append((ch.get("architecture"), entry.get("revision"), entry.get("version")))
    return out


def select_edge_revisions(channel_map, version, archs):
    """State of the unbranched latest/edge build set.

    Returns (selected, waiting, duplicates, legacy, malformed):
      selected    {arch: revision} for archs already at `version`
      waiting     archs still missing from edge or on an older version
      duplicates  archs with more than one edge entry (fail-closed condition)
      legacy      (arch, revision, version) entries outside the declared set
      malformed   (arch, problem) entries for supported archs that fail
                  validation (fail-closed condition, never "waiting")
    """
    by_arch = {}
    legacy = []
    malformed = []
    malformed_archs = set()
    for entry in channel_map:
        ch = entry.get("channel") or {}
        arch = ch.get("architecture")
        if ch.get("track") != "latest" or ch.get("risk") != "edge" or ch.get("branch") not in (None, ""):
            continue
        if arch in archs:
            problem = entry_problem(entry)
            if problem:
                malformed.append((arch, problem))
                malformed_archs.add(arch)
                continue
            by_arch.setdefault(arch, []).append((entry.get("revision"), entry.get("version")))
        else:
            legacy.append((arch, entry.get("revision"), entry.get("version")))
    selected = {}
    waiting = []
    duplicates = []
    for arch in sorted(archs):
        if arch in malformed_archs:
            continue  # reported as malformed; never treated as still building
        got = by_arch.get(arch, [])
        if not got:
            waiting.append(arch)
        elif len(got) > 1:
            duplicates.append(arch)
        else:
            rev, ver = got[0]
            if ver == version:
                selected[arch] = rev
            else:
                waiting.append(arch)
    return selected, waiting, duplicates, legacy, malformed


def version_key(version):
    """Semantic numeric tuple for version comparison. Never mutates channels."""
    return tuple(int(p) for p in re.findall(r"\d+", version or ""))


def stable_is_newer(stable_version, target_version):
    return version_key(stable_version) > version_key(target_version)


def arch_channel_state(channel_map, arch, risk, track="latest"):
    """Single (revision, version) for an unbranched arch/risk, or None when
    missing or duplicated. Used for read-only reporting and downgrade checks."""
    got = [(rev, ver) for a, rev, ver in channel_entries(channel_map, track, risk) if a == arch]
    if len(got) == 1:
        return got[0]
    return None


def promotion_selection(channel_map, version):
    """Select edge revisions for promotion at `version` (manual preflight).

    Required architectures must all be present at `version` on latest/edge;
    best-effort architectures are included only when already at `version`.
    Returns (selected, missing_required, best_effort_skipped, duplicates,
    malformed). Missing best-effort targets are never an error.
    """
    sel, waiting, duplicates, legacy, malformed = select_edge_revisions(
        channel_map, version, EXPECTED_ARCHS)
    missing_required = [a for a in sorted(REQUIRED_ARCHS) if a not in sel]
    best_effort_ready = {a: sel[a] for a in BEST_EFFORT_ARCHS if a in sel}
    best_effort_skipped = [a for a in sorted(BEST_EFFORT_ARCHS) if a not in sel]
    selected = {a: sel[a] for a in REQUIRED_ARCHS if a in sel}
    selected.update(best_effort_ready)
    return selected, missing_required, best_effort_skipped, duplicates, malformed


def candidate_selection(channel_map, version):
    """Candidate revisions at `version` for the stable phase.

    Required architectures must all be present; best-effort architectures are
    included only when staged on candidate at `version`. Returns (selected,
    missing_required).
    """
    selected = {}
    for a, rev, ver in channel_entries(channel_map, "latest", "candidate"):
        if a in EXPECTED_ARCHS and ver == version:
            selected[a] = rev
    missing_required = [a for a in sorted(REQUIRED_ARCHS) if a not in selected]
    return selected, missing_required


def downgrade_refusals(channel_map, selected, version):
    """Architectures whose current stable is semantically NEWER than `version`.

    Releasing the older revision would downgrade stable (e.g. a delayed older
    best-effort build after a newer release already landed). Returns sorted
    (arch, stable_version, stable_revision) tuples to refuse.
    """
    refusals = []
    for arch in sorted(selected):
        stable = arch_channel_state(channel_map, arch, "stable")
        if stable and stable_is_newer(stable[1], version):
            refusals.append((arch, stable[1], stable[0]))
    return refusals


def verify_channel(channel_map, risk, expected, track="latest"):
    """Verify an expected {arch: {"revision", "version"}} set in a channel.

    Entries for architectures outside `expected` are ignored (use
    legacy_entries to report them) and never fail verification. Returns
    (ok, problems).
    """
    problems = []
    by_arch = {}
    for entry in channel_map:
        ch = entry.get("channel") or {}
        arch = ch.get("architecture")
        if ch.get("track") == track and ch.get("risk") == risk and ch.get("branch") in (None, "") and arch in expected:
            problem = entry_problem(entry)
            if problem:
                problems.append("{} {} malformed: {}".format(arch, risk, problem))
                continue
            by_arch.setdefault(arch, []).append((entry.get("revision"), entry.get("version")))
    for arch, want in sorted(expected.items()):
        got = by_arch.get(arch, [])
        if not got:
            problems.append("missing {} entry for {}".format(risk, arch))
        elif len(got) > 1:
            problems.append("duplicate {} entries for {}: {}".format(risk, arch, got))
        else:
            rev, ver = got[0]
            if rev != want["revision"]:
                problems.append("{} {} revision {} != expected {}".format(arch, risk, rev, want["revision"]))
            if ver != want["version"]:
                problems.append("{} {} version {} != expected {}".format(arch, risk, ver, want["version"]))
    return (not problems, problems)


def verify_candidate_strict(channel_map, expected, supported):
    """Strict latest/candidate verification for the stable-release source.

    Unlike edge/stable, legacy entries cannot be tolerated here: the strict
    candidate set is the exact source the coordinator releases to stable, so an
    unsupported candidate entry means the source set is not the expected one.
    Returns (ok, problems); the coordinator fails closed and never auto-removes
    legacy candidate entries.
    """
    problems = []
    seen = {}
    for entry in channel_map:
        ch = entry.get("channel") or {}
        arch = ch.get("architecture")
        if ch.get("track") != "latest" or ch.get("risk") != "candidate" or ch.get("branch") not in (None, ""):
            continue
        if arch not in supported:
            problems.append("unsupported candidate entry {} revision {} version {}".format(
                arch, entry.get("revision"), entry.get("version")))
            continue
        problem = entry_problem(entry)
        if problem:
            problems.append("candidate {} malformed: {}".format(arch, problem))
            continue
        seen.setdefault(arch, []).append((entry.get("revision"), entry.get("version")))
    for arch, want in sorted(expected.items()):
        got = seen.get(arch, [])
        if not got:
            problems.append("missing candidate entry for {}".format(arch))
        elif len(got) > 1:
            problems.append("duplicate candidate entries for {}".format(arch))
        else:
            rev, ver = got[0]
            if rev != want["revision"]:
                problems.append("candidate {} revision {} != expected {}".format(arch, rev, want["revision"]))
            if ver != want["version"]:
                problems.append("candidate {} version {} != expected {}".format(arch, ver, want["version"]))
    return (not problems, problems)


def candidate_convergence(channel_map, expected, archs):
    """State of the latest/candidate stage.

    Returns (ok, retryable, problems). Unsupported, malformed or duplicate
    candidate entries are fatal (never retried): the strict candidate set is the
    exact source for the explicit per-revision stable releases. Missing or stale
    expected revisions are retryable while the Store channel map converges."""
    ok, problems = verify_candidate_strict(channel_map, expected, archs)
    if ok:
        return True, False, []
    fatal = [p for p in problems if any(k in p for k in ("unsupported", "malformed", "duplicate"))]
    if fatal:
        return False, False, fatal
    return False, True, problems


def stable_convergence(channel_map, expected, archs):
    """State of the latest/stable verification.

    Returns (ok, retryable, problems). Malformed or duplicate supported stable
    entries are fatal; missing or stale expected revisions are retryable while
    the Store channel map converges. Legacy entries are ignored."""
    ok, problems = verify_channel(channel_map, "stable", expected)
    if ok:
        return True, False, []
    fatal = [p for p in problems if any(k in p for k in ("malformed", "duplicate"))]
    if fatal:
        return False, False, fatal
    return False, True, problems


def poll_convergence(fetch, classify, expected, archs, wait_seconds, poll_seconds, what):
    """Poll until `classify` reports convergence. Returns (channel_map, ok, problems).

    Fatal problems return immediately; missing/stale entries are retried until
    the bounded deadline. A Store query failure fails closed."""
    deadline = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(seconds=wait_seconds)
    while True:
        try:
            channel_map = fetch()
        except (OSError, RuntimeError) as exc:
            return None, False, ["Store query failed: {}".format(exc)]
        ok, retryable, problems = classify(channel_map, expected, archs)
        if ok:
            return channel_map, True, None
        if not retryable:
            return channel_map, False, problems
        if datetime.datetime.now(datetime.timezone.utc) >= deadline:
            return channel_map, False, problems
        print("waiting for {} to converge: {}".format(what, "; ".join(problems)))
        time.sleep(poll_seconds)


def missing_rollback_archs(previous_stable, archs):
    """Architectures lacking a previous stable revision (rollback snapshot)."""
    return sorted(arch for arch in archs if arch not in previous_stable)


def legacy_entries(channel_map, risk, supported, track="latest"):
    return [
        (arch, rev, ver)
        for arch, rev, ver in channel_entries(channel_map, track, risk)
        if arch not in supported
    ]


def report_legacy(channel_map, risk, supported, track="latest"):
    for arch, rev, ver in legacy_entries(channel_map, risk, supported, track):
        print("ignoring legacy {} entry: {} revision {} version {} (not declared)".format(
            risk, arch, rev, ver))


def snapshot_stable(channel_map, supported, track="latest"):
    """{arch: {"revision", "version"}} for unbranched latest/stable (rollback base).

    Malformed supported entries are skipped; use stable_snapshot_problems to
    refuse publication before this map is relied upon."""
    out = {}
    for entry in channel_map:
        ch = entry.get("channel") or {}
        arch = ch.get("architecture")
        if ch.get("track") == track and ch.get("risk") == "stable" and ch.get("branch") in (None, "") and arch in supported:
            if entry_problem(entry) is None and arch not in out:
                out[arch] = {"revision": entry.get("revision"), "version": entry.get("version")}
    return out


def stable_snapshot_problems(channel_map, archs, track="latest"):
    """Problems that make the previous latest/stable snapshot unusable.

    Every supported architecture must have exactly one valid unbranched
    latest/stable entry (positive integer revision, non-empty version, valid
    architecture/track/risk). Malformed or duplicate supported entries and any
    missing architecture are all non-retryable problems. Legacy entries outside
    the declared set are ignored."""
    problems = []
    by_arch = {}
    for entry in channel_map:
        ch = entry.get("channel") or {}
        arch = ch.get("architecture")
        if ch.get("track") == track and ch.get("risk") == "stable" and ch.get("branch") in (None, "") and arch in archs:
            problem = entry_problem(entry)
            if problem:
                problems.append("previous stable {} malformed: {}".format(arch, problem))
                continue
            by_arch.setdefault(arch, []).append((entry.get("revision"), entry.get("version")))
    for arch in sorted(archs):
        got = by_arch.get(arch, [])
        if not got:
            problems.append("previous stable entry missing for {}".format(arch))
        elif len(got) > 1:
            problems.append("previous stable duplicate entries for {}: {}".format(arch, got))
    return problems


def candidate_at_version(channel_map, archs, version):
    """{arch: revision} if latest/candidate already holds exactly the expected
    set at this version (e.g. a prior stage of this coordinator); else None.
    Legacy entries outside the declared set are ignored."""
    by_arch = {}
    for arch, rev, ver in channel_entries(channel_map, "latest", "candidate"):
        if arch in archs:
            by_arch.setdefault(arch, []).append((rev, ver))
    if set(by_arch) != archs:
        return None
    result = {}
    for arch in archs:
        got = by_arch[arch]
        if len(got) != 1 or got[0][1] != version:
            return None
        result[arch] = got[0][0]
    return result


def build_rollback_commands(previous_stable, snap_name=SNAP_NAME):
    """Positional `snapcraft release` commands restoring the pre-release
    latest/stable map. Per-architecture and therefore not atomic; the workflow
    prints these and never executes them automatically."""
    commands = []
    for arch in sorted(previous_stable):
        commands.append("snapcraft release {} {} latest/stable".format(snap_name, previous_stable[arch]["revision"]))
    return commands


def rollback_availability(previous_stable, expected):
    """Split the pre-release snapshot into (known, unknown) rollback archs.

    An architecture whose stable entry already carries the target release
    version is not a known pre-release rollback base, whether that entry is the
    exact selected revision or any other revision of the target version. It may
    have been advanced by a prior partial publication of this coordinator or
    changed manually/by another publisher; the original pre-release revision
    cannot be reconstructed from the channel map. Only a stable entry from a
    version different from the target version is a known pre-release rollback
    revision. Returns (known, unknown) where known is {arch: {"revision",
    "version"}} and unknown is a sorted list of architecture names."""
    known = {}
    unknown = []
    for arch in sorted(previous_stable):
        want = expected.get(arch, {})
        prev = previous_stable[arch]
        already_target_version = prev.get("version") == want.get("version")
        if prev["revision"] == want.get("revision") or already_target_version:
            unknown.append(arch)
        else:
            known[arch] = prev
    return known, unknown


def print_rollback(previous_stable, expected, snap_name=SNAP_NAME):
    """Honest rollback reporting for the previous-stable snapshot.

    On a pristine run no supported architecture exposes the target release
    version yet and the full rollback set is authoritative. On a partial rerun,
    architectures whose stable already exposes the target release version have
    an unknown original pre-release revision and are reported explicitly rather
    than as a misleading complete rollback plan. Never labels a mixed snapshot
    as an authoritative full rollback."""
    known, unknown = rollback_availability(previous_stable, expected)
    if unknown:
        print("Original pre-release stable revision is not recoverable from the channel map for: "
              "{} (stable already exposes the target release version).".format(
                  ", ".join(unknown)))
    if known:
        print("Rollback commands (per-arch; only architectures whose original pre-release "
              "revision is known):")
        for command in build_rollback_commands(known, snap_name):
            print("  " + command)
    elif unknown:
        print("No rollback commands are available: no supported architecture exposes a "
              "known original pre-release revision. Resume publication instead of rolling back.")


def release_command(revision, snap_name=SNAP_NAME):
    return ["snapcraft", "release", snap_name, str(revision), "latest/candidate"]


def stable_release_command(revision, snap_name=SNAP_NAME):
    """Explicit per-revision stable release (idempotent, never whole-channel promote).

    `snapcraft promote` applies a completeness policy over the entire set of
    ever-released store architectures, which includes the historical i386 entry
    Nift no longer declares. Releasing each selected candidate revision directly
    to latest/stable is the supported way to promote exactly the six declared
    architectures while leaving i386 untouched."""
    return ["snapcraft", "release", snap_name, str(revision), "latest/stable"]


def smoke_command(version, amd64_revision):
    return ["bash", "packaging/snap-candidate-smoke.sh", str(version), str(amd64_revision)]


def credentials_present():
    return bool(os.environ.get("SNAPCRAFT_STORE_CREDENTIALS"))


def run_snapcraft(argv, dry_run):
    if dry_run:
        print("DRY-RUN: " + " ".join(argv))
        return 0
    result = subprocess.run(argv)
    return result.returncode


def parse_mode(argv):
    args = list(argv if argv is not None else sys.argv[1:])
    dry_run = "--dry-run" in args
    for mode in ("--status", "--check-ready", "--promote-candidate", "--promote-stable"):
        if mode in args:
            return mode[2:], dry_run
    return "status", dry_run


def confirm_ok():
    return os.environ.get("NIFT_SNAP_CONFIRM") == "yes"


def cmd_status(channel_map, version, archs):
    print("Snap state for {} (target {})".format(SNAP_NAME, version))
    for arch in sorted(archs):
        kind = "required" if arch in REQUIRED_ARCHS else "best-effort"
        edge = arch_channel_state(channel_map, arch, "edge")
        cand = arch_channel_state(channel_map, arch, "candidate")
        stab = arch_channel_state(channel_map, arch, "stable")
        def fmt(state):
            return "{} rev={}".format(state[1], state[0]) if state else "-"
        if edge and edge[1] == version:
            edge_state = "ready"
        elif edge is None:
            edge_state = "pending"
        elif version_key(edge[1]) < version_key(version):
            edge_state = "older"
        else:
            edge_state = "newer"
        print("  {:10s} {:11s} edge={} ({}) candidate={} stable={}".format(
            arch, kind, fmt(edge), edge_state, fmt(cand), fmt(stab)))
    return 0


def cmd_check_ready(channel_map, version, archs):
    selected, missing_required, best_effort_skipped, duplicates, malformed = promotion_selection(
        channel_map, version)
    if duplicates:
        print("FAIL: duplicate latest/edge entries for archs: {}".format(", ".join(duplicates)), file=sys.stderr)
        return 1
    if malformed:
        print("FAIL: malformed latest/edge entries for supported archs:", file=sys.stderr)
        for arch, problem in malformed:
            print("  {}: {}".format(arch, problem), file=sys.stderr)
        return 1
    if missing_required:
        print("NOT READY: required architecture(s) missing from latest/edge at {}: {}".format(
            version, ", ".join(missing_required)), file=sys.stderr)
        return 1
    print("READY: required architectures all at {} on latest/edge: {}".format(
        version, ", ".join(sorted(REQUIRED_ARCHS))))
    for arch in best_effort_skipped:
        edge = arch_channel_state(channel_map, arch, "edge")
        if edge is None:
            print("  best-effort {}: no latest/edge build yet (pending)".format(arch))
        elif version_key(edge[1]) < version_key(version):
            print("  best-effort {}: latest/edge still at older {} (promote separately later)".format(
                arch, edge[1]))
        else:
            print("  best-effort {}: latest/edge at {} (newer than target; inspect)".format(arch, edge[1]))
    return 0


def cmd_promote_candidate(channel_map, version, archs, dry_run):
    candidate_wait = int(os.environ.get("NIFT_SNAP_CANDIDATE_WAIT", "300"))
    poll_seconds = int(os.environ.get("NIFT_SNAP_POLL", "20"))

    selected, missing_required, best_effort_skipped, duplicates, malformed = promotion_selection(
        channel_map, version)
    if duplicates:
        print("FAIL: duplicate latest/edge entries for archs: {}".format(", ".join(duplicates)), file=sys.stderr)
        return 1
    if malformed:
        print("FAIL: malformed latest/edge entries for supported archs:", file=sys.stderr)
        for arch, problem in malformed:
            print("  {}: {}".format(arch, problem), file=sys.stderr)
        return 1
    if missing_required:
        print("FAIL: required architecture(s) missing from latest/edge at {}: {}".format(
            version, ", ".join(missing_required)), file=sys.stderr)
        print("No candidate mutation performed. Inspect the Snap build page and retry later.", file=sys.stderr)
        return 1
    for arch in best_effort_skipped:
        print("SKIP best-effort {}: not at {} on latest/edge; required set proceeds without it".format(
            arch, version))

    # A usable rollback snapshot for the required set is required before any
    # candidate mutation. Best-effort stable state (older/pending/newer) is not
    # part of this rollback base.
    snapshot_problems = stable_snapshot_problems(channel_map, REQUIRED_ARCHS)
    if snapshot_problems:
        print("FAIL: invalid previous stable snapshot:\n  " + "\n  ".join(snapshot_problems), file=sys.stderr)
        print("Refusing any candidate mutation (no approved first-release exception in place).", file=sys.stderr)
        return 1
    previous_stable = snapshot_stable(channel_map, REQUIRED_ARCHS)

    expected = {arch: {"revision": rev, "version": version} for arch, rev in selected.items()}
    _, already_target = rollback_availability(previous_stable, expected)
    if already_target:
        print("NOTE: previous stable already exposes the target release version for "
              "{}; those original pre-release revisions are not recoverable from the "
              "channel map. This run resumes publication toward the exact selected "
              "revisions. Assignments already at their selected revision remain "
              "unchanged; other revisions of the target version may be corrected."
              .format(", ".join(already_target)))

    if candidate_at_version(channel_map, set(selected), version) is None:
        for arch in sorted(selected):
            if run_snapcraft(release_command(expected[arch]["revision"]), dry_run) != 0:
                print("FAIL: release revision {} to candidate".format(expected[arch]["revision"]), file=sys.stderr)
                return 1

    channel_map, ok, problems = poll_convergence(
        fetch_channel_map, candidate_convergence, expected, set(selected), candidate_wait, poll_seconds, "candidate")
    if not ok:
        print("FAIL: candidate did not converge:\n  " + "\n  ".join(problems or []), file=sys.stderr)
        print("Manual inspection required: review candidate entries in the Snap Store; the "
              "coordinator never modifies unsupported candidate entries.", file=sys.stderr)
        return 1
    report_legacy(channel_map, "candidate", EXPECTED_ARCHS)
    print("Candidate staged and verified at {} for: {}".format(version, ", ".join(sorted(selected))))
    return 0


def cmd_promote_stable(channel_map, version, archs, dry_run):
    stable_wait = int(os.environ.get("NIFT_SNAP_STABLE_WAIT", "300"))
    poll_seconds = int(os.environ.get("NIFT_SNAP_POLL", "20"))

    selected, missing_required = candidate_selection(channel_map, version)
    if missing_required:
        print("FAIL: required architecture(s) missing from latest/candidate at {}: {}".format(
            version, ", ".join(missing_required)), file=sys.stderr)
        print("Run --promote-candidate after edge is complete, or inspect candidate.", file=sys.stderr)
        return 1

    # Refuse any downgrade of a NEWER stable revision (e.g. a delayed older
    # best-effort build after a newer release landed). A required architecture
    # with a newer stable means the target version is stale: fail. A best-effort
    # architecture is skipped so the required set still publishes.
    refusals = downgrade_refusals(channel_map, selected, version)
    required_refusals = [(a, v, r) for a, v, r in refusals if a in REQUIRED_ARCHS]
    if required_refusals:
        for arch, stable_version, stable_rev in required_refusals:
            print("FAIL: refusing to downgrade {} latest/stable ({} rev {}) to {}".format(
                arch, stable_version, stable_rev, version), file=sys.stderr)
        return 1
    for arch, stable_version, stable_rev in refusals:
        print("SKIP best-effort {}: latest/stable is newer ({} rev {}); refusing to downgrade to {}".format(
            arch, stable_version, stable_rev, version))
        del selected[arch]

    expected = {arch: {"revision": rev, "version": version} for arch, rev in selected.items()}
    previous_stable = snapshot_stable(channel_map, REQUIRED_ARCHS)

    _, already_target = rollback_availability(previous_stable, expected)
    if already_target:
        print("NOTE: previous stable already exposes the target release version for "
              "{}; those original pre-release revisions are not recoverable from the "
              "channel map. This run resumes publication toward the exact selected "
              "revisions. Assignments already at their selected revision remain "
              "unchanged; other revisions of the target version may be corrected."
              .format(", ".join(already_target)))

    if "amd64" not in selected:
        print("FAIL: amd64 is required but missing from the selected candidate set", file=sys.stderr)
        return 1
    if run_snapcraft(smoke_command(version, expected["amd64"]["revision"]), dry_run) != 0:
        print("FAIL: candidate confinement smoke failed; stable not released", file=sys.stderr)
        return 1

    try:
        channel_map = fetch_channel_map()
    except (OSError, RuntimeError) as exc:
        print("FAIL: could not query the Snap Store before stable release: {}".format(exc), file=sys.stderr)
        return 1
    ok, problems = verify_candidate_strict(channel_map, expected, set(selected) | BEST_EFFORT_ARCHS)
    if not ok:
        print("FAIL: candidate revalidation before stable release:\n  " + "\n  ".join(problems), file=sys.stderr)
        print("No stable mutation performed.", file=sys.stderr)
        return 1

    released = []
    for arch in sorted(expected):
        if run_snapcraft(stable_release_command(expected[arch]["revision"]), dry_run) != 0:
            print("FAIL: stable publication is PARTIAL after {}/{} architectures:".format(
                len(released), len(expected)), file=sys.stderr)
            for done_arch in sorted(released):
                print("  {} -> revision {} at latest/stable (correct, preserved on rerun)".format(
                    done_arch, expected[done_arch]["revision"]), file=sys.stderr)
            print("  Pending (safe to rerun; per-revision releases are idempotent): "
                  "{}".format(", ".join(a for a in sorted(expected) if a not in released)), file=sys.stderr)
            print_rollback(previous_stable, expected)
            return 1
        released.append(arch)

    channel_map, ok, problems = poll_convergence(
        fetch_channel_map, stable_convergence, expected, set(selected), stable_wait, poll_seconds, "stable")
    if not ok:
        print("FAIL: stable did not converge:\n  " + "\n  ".join(problems or []), file=sys.stderr)
        print_rollback(previous_stable, expected)
        return 1
    report_legacy(channel_map, "stable", EXPECTED_ARCHS)

    missing_best_effort = sorted(BEST_EFFORT_ARCHS - set(selected))
    print("Stable verified at {} for: {}".format(version, ", ".join(sorted(selected))))
    if missing_best_effort:
        print("Best-effort not promoted (not staged on candidate at {}): {}".format(
            version, ", ".join(missing_best_effort)))
    print_rollback(previous_stable, expected)
    return 0


def main(argv=None):
    mode, dry_run = parse_mode(argv)
    version = os.environ.get("NIFT_SNAP_VERSION")
    if not version:
        print("FAIL: NIFT_SNAP_VERSION is required", file=sys.stderr)
        return 2
    if mode in ("promote-candidate", "promote-stable"):
        if not confirm_ok():
            print("FAIL: NIFT_SNAP_CONFIRM=yes is required for {}".format(mode), file=sys.stderr)
            return 2
        if not dry_run and not credentials_present():
            print("FAIL: SNAPCRAFT_STORE_CREDENTIALS is required for release", file=sys.stderr)
            return 2

    archs = load_platforms()
    if not archs:
        print("FAIL: could not read platforms from snap/snapcraft.yaml", file=sys.stderr)
        return 2

    try:
        channel_map = fetch_channel_map()
    except (OSError, RuntimeError) as exc:
        print("FAIL: could not query the Snap Store: {}".format(exc), file=sys.stderr)
        return 1

    if mode == "status":
        return cmd_status(channel_map, version, archs)
    if mode == "check-ready":
        return cmd_check_ready(channel_map, version, archs)
    if mode == "promote-candidate":
        return cmd_promote_candidate(channel_map, version, archs, dry_run)
    if mode == "promote-stable":
        return cmd_promote_stable(channel_map, version, archs, dry_run)
    return 2


if __name__ == "__main__":
    sys.exit(main())