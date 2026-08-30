#!/usr/bin/env python3
"""Snap release coordinator for the nift snap.

The Snapcraft/Launchpad build service connected to the GitHub repository reads
snap/snapcraft.yaml and produces a Store revision for every declared platform on
each tag push. This coordinator is the sole publisher: it waits for the
build-service revisions of the tagged version, releases exactly those revisions
to latest/candidate, verifies the complete six-architecture build set, promotes
the set to latest/stable, and verifies stable. GitHub-built snaps are never
uploaded to the Store during ordinary tag releases.

The decision logic is pure and importable for offline tests; running this module
executes the real Store transaction (unless --dry-run is given).

Environment:
  SNAP_NAME                   snap name (default: nift)
  NIFT_SNAP_VERSION           tag version to release (no leading 'v')
  NIFT_SNAP_START             ISO-8601 UTC workflow start time (correlation base)
  NIFT_SNAP_WAIT              seconds to wait for build-service revisions (default 5400)
  NIFT_SNAP_TOLERANCE         seconds accepted before NIFT_SNAP_START (default 600)
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


class ReleaseSelectionError(Exception):
    def __init__(self, message, competing=None):
        super().__init__(message)
        self.competing = competing  # dict arch -> [revision numbers]


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


def fetch_info(snap_name=SNAP_NAME, fields="channel-map,revisions"):
    url = "{}/{}?fields={}".format(STORE_API, snap_name, fields)
    # The v2 info endpoint requires the Snap-Device-Series header.
    req = urllib.request.Request(
        url, headers={"Snap-Device-Series": "16", "User-Agent": "nift-release-coordinator/1"}
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.load(resp)


def parse_iso(value):
    text = str(value).strip()
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    return datetime.datetime.fromisoformat(text).astimezone(datetime.timezone.utc)


def revision_created(revision):
    created = revision.get("created_at")
    if not created:
        return None
    try:
        return parse_iso(created)
    except (ValueError, TypeError):
        return None


def revision_on_channel(revision, channel):
    return channel in revision.get("channels", [])


def revision_archs(revision):
    return set(revision.get("architectures", []))


def select_revisions(revisions, version, archs, start, now, tolerance=600, exclude_stable=True):
    """Return {arch: revision_number} for the current build-service run.

    Only revisions of the exact version, built for an expected architecture,
    created within [start - tolerance, now], and not already on latest/stable
    are eligible. Exactly one eligible revision per architecture is required;
    ambiguity or a missing architecture raises ReleaseSelectionError so the
    caller fails closed with stable untouched.
    """
    start_dt = parse_iso(start)
    candidates = {}
    for rev in revisions:
        if rev.get("version") != version:
            continue
        rarchs = revision_archs(rev)
        if not (rarchs & archs):
            continue
        if exclude_stable and revision_on_channel(rev, "latest/stable"):
            continue
        created = revision_created(rev)
        if created is None:
            continue
        if created < start_dt - datetime.timedelta(seconds=tolerance) or created > now:
            continue
        for arch in rarchs & archs:
            candidates.setdefault(arch, []).append(rev["revision"])

    competing = {}
    for arch in sorted(archs):
        bucket = candidates.get(arch, [])
        if len(bucket) > 1:
            competing[arch] = sorted(bucket)
    if competing:
        raise ReleaseSelectionError(
            "ambiguous eligible revisions for version {}: {}".format(
                version, "; ".join("{}={}".format(a, b) for a, b in competing.items())
            ),
            competing=competing,
        )
    missing = sorted(a for a in archs if a not in candidates)
    if missing:
        raise ReleaseSelectionError("no eligible revision for archs: {}".format(", ".join(missing)))
    return {arch: candidates[arch][0] for arch in archs}


def channel_entries(channel_map, track, risk):
    """(architecture, revision, version) for unbranched track/risk entries."""
    out = []
    for entry in channel_map:
        ch = entry.get("channel") or {}
        if ch.get("track") == track and ch.get("risk") == risk and ch.get("branch") is None:
            out.append((entry.get("architecture"), entry.get("revision"), entry.get("version")))
    return out


def verify_channel(channel_map, risk, expected, track="latest"):
    """expected: {arch: {"revision": int, "version": str}}. Returns (ok, problems)."""
    problems = []
    by_arch = {}
    for arch, rev, ver in channel_entries(channel_map, track, risk):
        by_arch.setdefault(arch, []).append((rev, ver))
    unexpected = sorted(set(by_arch) - set(expected))
    if unexpected:
        problems.append("unexpected {} entries for archs: {}".format(risk, ", ".join(unexpected)))
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


def snapshot_stable(channel_map, track="latest"):
    """{arch: {"revision", "version"}} for unbranched latest/stable (rollback base)."""
    out = {}
    for arch, rev, ver in channel_entries(channel_map, track, "stable"):
        if arch not in out:
            out[arch] = {"revision": rev, "version": ver}
    return out


def candidate_at_version(info, archs, version):
    """{arch: revision} if latest/candidate already holds exactly the expected
    set at this version (e.g. a prior stage of this coordinator); else None."""
    by_arch = {}
    for arch, rev, ver in channel_entries(info.get("channel-map", []), "latest", "candidate"):
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
    start = os.environ.get("NIFT_SNAP_START")
    if not version:
        print("FAIL: NIFT_SNAP_VERSION is required", file=sys.stderr)
        return 2
    if not start:
        print("FAIL: NIFT_SNAP_START is required", file=sys.stderr)
        return 2
    if not dry_run and not credentials_present():
        print("FAIL: SNAPCRAFT_STORE_CREDENTIALS is required for release/promote", file=sys.stderr)
        return 2
    wait_seconds = int(os.environ.get("NIFT_SNAP_WAIT", "5400"))
    tolerance = int(os.environ.get("NIFT_SNAP_TOLERANCE", "600"))

    archs = load_platforms()
    if not archs:
        print("FAIL: could not read platforms from snap/snapcraft.yaml", file=sys.stderr)
        return 2

    try:
        info = fetch_info()
    except OSError as exc:
        print("FAIL: could not query the Snap Store: {}".format(exc), file=sys.stderr)
        return 1
    previous_stable = snapshot_stable(info.get("channel-map", []))

    selected = candidate_at_version(info, archs, version)
    if selected is None:
        deadline = parse_iso(start) + datetime.timedelta(seconds=wait_seconds)
        while True:
            try:
                info = fetch_info()
                selected = select_revisions(
                    info.get("revisions", []), version, archs, start,
                    datetime.datetime.now(datetime.timezone.utc), tolerance,
                )
                break
            except ReleaseSelectionError as exc:
                if "no eligible revision" in str(exc) and datetime.datetime.now(datetime.timezone.utc) < deadline:
                    time.sleep(30)
                    continue
                print("FAIL: {}".format(exc), file=sys.stderr)
                return 1

    expected = {arch: {"revision": rev, "version": version} for arch, rev in selected.items()}

    if candidate_at_version(info, archs, version) is None:
        for arch in sorted(expected):
            if run_snapcraft(release_command(expected[arch]["revision"]), dry_run) != 0:
                print("FAIL: release revision {} to candidate".format(expected[arch]["revision"]), file=sys.stderr)
                return 1

    info = fetch_info()
    ok, problems = verify_channel(info.get("channel-map", []), "candidate", expected)
    if not ok:
        print("FAIL: candidate verification:\n  " + "\n  ".join(problems), file=sys.stderr)
        return 1

    if run_snapcraft(promote_command(), dry_run) != 0:
        print("FAIL: promote candidate -> stable", file=sys.stderr)
        return 1

    info = fetch_info()
    ok, problems = verify_channel(info.get("channel-map", []), "stable", expected)
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