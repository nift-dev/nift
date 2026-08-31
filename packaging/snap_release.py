#!/usr/bin/env python3
"""Snap release coordinator for the nift snap.

The Snapcraft/Launchpad build service connected to the GitHub repository reads
snap/snapcraft.yaml and publishes repository builds for the six declared
platforms to latest/edge. This coordinator is the sole publisher: it waits for
every supported architecture to reach the tagged version on latest/edge,
releases exactly those edge revisions to latest/candidate, verifies the complete
six-architecture candidate set with no unsupported entries, runs the amd64
candidate confinement smoke, promotes the set to latest/stable, and verifies
stable. GitHub-built snaps are never uploaded to the Store during ordinary
release runs.

Legacy channel entries outside the declared platform set (e.g. i386 at an old
version) are ignored and reported in edge and stable, and never promoted,
replaced or closed. In candidate they are a fail-closed condition: `snapcraft
promote` moves the whole candidate build set, so an unsupported candidate entry
would be promoted alongside the supported six.

The decision logic is pure and importable for offline tests; running this module
executes the real Store transaction (unless --dry-run is given).

Environment:
  SNAP_NAME                   snap name (default: nift)
  NIFT_SNAP_VERSION           release version to coordinate (no leading 'v')
  NIFT_SNAP_WAIT              seconds to wait for edge builds to reach the
                              version (default 7200)
  SNAPCRAFT_STORE_CREDENTIALS required for any release/promote operation
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
EXPECTED_ARCHS = {"amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"}


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
    """Strict latest/candidate verification for the promote source.

    Unlike edge/stable, legacy entries cannot be tolerated here: `snapcraft
    promote` moves the entire candidate build set, so an unsupported entry in
    candidate would be promoted alongside the supported six. Returns
    (ok, problems); the coordinator fails closed and never auto-removes legacy
    candidate entries.
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
    """{arch: {"revision", "version"}} for unbranched latest/stable (rollback base)."""
    out = {}
    for arch, rev, ver in channel_entries(channel_map, track, "stable"):
        if arch in supported and arch not in out:
            out[arch] = {"revision": rev, "version": ver}
    return out


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
    """Positional `snapcraft release` commands restoring the pre-promotion
    latest/stable map. Per-architecture and therefore not atomic; the workflow
    prints these and never executes them automatically."""
    commands = []
    for arch in sorted(previous_stable):
        commands.append("snapcraft release {} {} latest/stable".format(snap_name, previous_stable[arch]["revision"]))
    return commands


def release_command(revision, snap_name=SNAP_NAME):
    return ["snapcraft", "release", snap_name, str(revision), "latest/candidate"]


def promote_command(snap_name=SNAP_NAME):
    return ["snapcraft", "promote", snap_name, "--from-channel=latest/candidate", "--to-channel=latest/stable", "--yes"]


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


def main(argv=None):
    dry_run = "--dry-run" in (argv if argv is not None else sys.argv[1:])
    version = os.environ.get("NIFT_SNAP_VERSION")
    if not version:
        print("FAIL: NIFT_SNAP_VERSION is required", file=sys.stderr)
        return 2
    if not dry_run and not credentials_present():
        print("FAIL: SNAPCRAFT_STORE_CREDENTIALS is required for release/promote", file=sys.stderr)
        return 2
    wait_seconds = int(os.environ.get("NIFT_SNAP_WAIT", "7200"))
    poll_seconds = int(os.environ.get("NIFT_SNAP_POLL", "30"))

    archs = load_platforms()
    if not archs:
        print("FAIL: could not read platforms from snap/snapcraft.yaml", file=sys.stderr)
        return 2

    try:
        channel_map = fetch_channel_map()
    except (OSError, RuntimeError) as exc:
        print("FAIL: could not query the Snap Store: {}".format(exc), file=sys.stderr)
        return 1
    previous_stable = snapshot_stable(channel_map, archs)

    # A complete rollback snapshot is required: every supported architecture
    # must already have a previous stable revision or promotion is refused.
    missing_rollback = missing_rollback_archs(previous_stable, archs)
    if missing_rollback:
        print("FAIL: no previous stable revision for archs: {}; refusing promotion "
              "(no approved first-release exception in place)".format(", ".join(missing_rollback)),
              file=sys.stderr)
        return 1

    selected = candidate_at_version(channel_map, archs, version)
    if selected is None:
        deadline = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(seconds=wait_seconds)
        while True:
            try:
                channel_map = fetch_channel_map()
            except (OSError, RuntimeError) as exc:
                print("FAIL: could not query the Snap Store: {}".format(exc), file=sys.stderr)
                return 1
            report_legacy(channel_map, "edge", archs)
            sel, waiting, duplicates, legacy, malformed = select_edge_revisions(channel_map, version, archs)
            if duplicates:
                print("FAIL: duplicate latest/edge entries for archs: {}".format(", ".join(duplicates)), file=sys.stderr)
                return 1
            if malformed:
                print("FAIL: malformed latest/edge entries for supported archs:", file=sys.stderr)
                for arch, problem in malformed:
                    print("  {}: {}".format(arch, problem), file=sys.stderr)
                return 1
            if not waiting:
                selected = sel
                break
            if datetime.datetime.now(datetime.timezone.utc) >= deadline:
                print("FAIL: timed out waiting for latest/edge to reach version {}; still waiting: {}".format(
                    version, ", ".join(waiting)), file=sys.stderr)
                return 1
            time.sleep(poll_seconds)

    expected = {arch: {"revision": rev, "version": version} for arch, rev in selected.items()}

    if candidate_at_version(channel_map, archs, version) is None:
        for arch in sorted(expected):
            if run_snapcraft(release_command(expected[arch]["revision"]), dry_run) != 0:
                print("FAIL: release revision {} to candidate".format(expected[arch]["revision"]), file=sys.stderr)
                return 1

    channel_map = fetch_channel_map()
    report_legacy(channel_map, "candidate", archs)
    # Strict candidate verification: exactly the six declared architectures, no
    # unsupported entries, exact selected revisions, exact version, unbranched
    # latest/candidate. Legacy entries in candidate must fail closed before
    # promotion; they are never auto-removed.
    ok, problems = verify_candidate_strict(channel_map, expected, archs)
    if not ok:
        print("FAIL: candidate verification (promotion refused):\n  " + "\n  ".join(problems), file=sys.stderr)
        print("Manual inspection required: review and remove any unsupported candidate "
              "entries in the Snap Store, then rerun. The coordinator does not modify them.",
              file=sys.stderr)
        return 1

    if run_snapcraft(smoke_command(version, expected["amd64"]["revision"]), dry_run) != 0:
        print("FAIL: candidate confinement smoke failed; stable not promoted", file=sys.stderr)
        return 1

    if run_snapcraft(promote_command(), dry_run) != 0:
        print("FAIL: promote candidate -> stable", file=sys.stderr)
        return 1

    channel_map = fetch_channel_map()
    report_legacy(channel_map, "stable", archs)
    ok, problems = verify_channel(channel_map, "stable", expected)
    if not ok:
        print("FAIL: stable verification:\n  " + "\n  ".join(problems), file=sys.stderr)
        print("Rollback commands (manual, per-arch, non-atomic):")
        for command in build_rollback_commands(previous_stable):
            print("  " + command)
        return 1

    print("Stable verified: all six architectures at version {}".format(version))
    print("Rollback commands (previous stable, per-arch):")
    for command in build_rollback_commands(previous_stable):
        print("  " + command)
    return 0


if __name__ == "__main__":
    sys.exit(main())