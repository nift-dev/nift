#!/usr/bin/env python3
"""Snap release coordinator for the nift snap.

The Snapcraft/Launchpad build service connected to the GitHub repository reads
snap/snapcraft.yaml and publishes one build per declared platform into
latest/edge on each tag push. This coordinator is the sole publisher: it waits
for every supported architecture to reach the tagged version on latest/edge,
releases exactly those edge revisions to latest/candidate, verifies the complete
six-architecture candidate set, runs the candidate confinement smoke on amd64,
promotes the set to latest/stable, and verifies stable. GitHub-built snaps are
never uploaded to the Store during ordinary tag releases.

Legacy channel entries outside the declared platform set (e.g. i386 at an old
version) are ignored and reported, never promoted, replaced or closed.

The decision logic is pure and importable for offline tests; running this module
executes the real Store transaction (unless --dry-run is given).

Environment:
  SNAP_NAME                   snap name (default: nift)
  NIFT_SNAP_VERSION           tag version to release (no leading 'v')
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
    url = "{}/{}?fields=channel-map".format(STORE_API, snap_name)
    # The v2 info endpoint requires the Snap-Device-Series header.
    req = urllib.request.Request(
        url, headers={"Snap-Device-Series": "16", "User-Agent": "nift-release-coordinator/1"}
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        payload = json.load(resp)
    return parse_channel_map_response(payload)


def channel_entries(channel_map, track, risk):
    """(architecture, revision, version) for unbranched track/risk entries.

    Matches the real v2 channel-map schema: architecture lives inside `channel`,
    and revision/version are top-level. A missing or null branch means the
    channel is unbranched.
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

    Returns (selected, waiting, duplicates, legacy):
      selected    {arch: revision} for archs already at `version`
      waiting     archs still missing from edge or on an older version
      duplicates  archs with more than one edge entry (fail-closed condition)
      legacy      (arch, revision, version) entries outside the declared set
    """
    by_arch = {}
    legacy = []
    for arch, rev, ver in channel_entries(channel_map, "latest", "edge"):
        if arch in archs:
            by_arch.setdefault(arch, []).append((rev, ver))
        else:
            legacy.append((arch, rev, ver))
    selected = {}
    waiting = []
    duplicates = []
    for arch in sorted(archs):
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
    return selected, waiting, duplicates, legacy


def verify_channel(channel_map, risk, expected, track="latest"):
    """Verify an expected {arch: {"revision", "version"}} set in a channel.

    Entries for architectures outside `expected` are ignored (use
    legacy_entries to report them) and never fail verification. Returns
    (ok, problems).
    """
    problems = []
    by_arch = {}
    for arch, rev, ver in channel_entries(channel_map, track, risk):
        if arch in expected:
            by_arch.setdefault(arch, []).append((rev, ver))
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
            sel, waiting, duplicates, legacy = select_edge_revisions(channel_map, version, archs)
            if duplicates:
                print("FAIL: duplicate latest/edge entries for archs: {}".format(", ".join(duplicates)), file=sys.stderr)
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
    ok, problems = verify_channel(channel_map, "candidate", expected)
    if not ok:
        print("FAIL: candidate verification:\n  " + "\n  ".join(problems), file=sys.stderr)
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