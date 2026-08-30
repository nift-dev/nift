#!/usr/bin/env python3
"""Offline contract tests for the nift Snap publication coordinator.

Covers: the exact six-architecture set; candidate staging (never a direct tag
release to stable); correlation of revisions to the current build-service run by
timestamp; complete-set verification before promotion; the exact promote --yes
invocation; unbranched channel filtering; historical unreleased revisions not
treated as duplicate candidate entries; missing/extra/duplicated/wrong-version
fail-closed handling; positional rollback syntax; post-promotion verification;
workflow_call execution from release.yml; and manual edge publication remaining
isolated. No network access and no Store operations are performed.
"""

import datetime
import io
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "packaging"))
sys.path.insert(0, os.path.dirname(__file__))

import snap_release as sr

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
VERSION = "4.0.9"
START = "2026-08-31T00:00:00Z"
NOW = datetime.datetime(2026, 8, 31, 1, 0, 0, tzinfo=datetime.timezone.utc)
TOL = 600


def revision(num, arch, version=VERSION, created="2026-08-31T00:01:00Z", channels=None):
    return {
        "revision": num,
        "version": version,
        "architectures": [arch],
        "created_at": created,
        "channels": channels or [],
    }


def channel_entry(arch, rev, version=VERSION, risk="candidate", branch=None, track="latest"):
    return {
        "channel": {"track": track, "risk": risk, "branch": branch},
        "architecture": arch,
        "revision": rev,
        "version": version,
    }


def six_revisions(version=VERSION, channels=None):
    return [
        revision(i, arch, version=version, channels=channels)
        for i, arch in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)
    ]


EXPECTED = {arch: {"revision": i, "version": VERSION}
            for i, arch in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)}


class PlatformsContract(unittest.TestCase):
    def test_declared_platforms_match_contract(self):
        with open(os.path.join(REPO, "snap", "snapcraft.yaml"), encoding="utf-8") as f:
            archs = sr.parse_platforms(f.read())
        self.assertEqual(archs, sr.EXPECTED_ARCHS)

    def test_parse_platforms_ignores_non_platform_sections(self):
        yaml = "name: nift\nplatforms:\n  amd64:\n  arm64:\n\napps:\n  nift:\n"
        self.assertEqual(sr.parse_platforms(yaml), {"amd64", "arm64"})


class RevisionSelection(unittest.TestCase):
    def test_selects_one_revision_per_arch_in_window(self):
        selected = sr.select_revisions(six_revisions(), VERSION, sr.EXPECTED_ARCHS, START, NOW, TOL)
        self.assertEqual({a: i for i, a in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)}, selected)

    def test_missing_architecture_fails_closed(self):
        revs = six_revisions()[:-1]  # drop s390x
        with self.assertRaises(sr.ReleaseSelectionError) as ctx:
            sr.select_revisions(revs, VERSION, sr.EXPECTED_ARCHS, START, NOW, TOL)
        self.assertIn("s390x", str(ctx.exception))

    def test_empty_revision_list_fails_closed(self):
        with self.assertRaises(sr.ReleaseSelectionError):
            sr.select_revisions([], VERSION, sr.EXPECTED_ARCHS, START, NOW, TOL)

    def test_duplicate_eligible_revisions_fail_closed(self):
        revs = six_revisions() + [revision(99, "amd64", created="2026-08-31T00:02:00Z")]
        with self.assertRaises(sr.ReleaseSelectionError) as ctx:
            sr.select_revisions(revs, VERSION, sr.EXPECTED_ARCHS, START, NOW, TOL)
        self.assertIn("amd64", str(ctx.exception))
        self.assertEqual(ctx.exception.competing, {"amd64": [1, 99]})

    def test_historical_unreleased_revision_is_not_a_duplicate(self):
        # An old revision created long before this run is not an eligible
        # candidate and must not make the selection ambiguous.
        revs = six_revisions() + [revision(98, "amd64", created="2026-01-01T00:00:00Z")]
        selected = sr.select_revisions(revs, VERSION, sr.EXPECTED_ARCHS, START, NOW, TOL)
        self.assertEqual(selected["amd64"], 1)

    def test_stable_revisions_are_excluded(self):
        revs = [revision(1, "amd64", channels=["latest/stable"])] + six_revisions()[1:]
        with self.assertRaises(sr.ReleaseSelectionError):
            sr.select_revisions(revs, VERSION, sr.EXPECTED_ARCHS, START, NOW, TOL)

    def test_wrong_version_excluded(self):
        revs = six_revisions(version="4.0.8")
        with self.assertRaises(sr.ReleaseSelectionError):
            sr.select_revisions(revs, VERSION, sr.EXPECTED_ARCHS, START, NOW, TOL)


class ChannelVerification(unittest.TestCase):
    def build_map(self, entries):
        return {"channel-map": entries, "revisions": six_revisions()}

    def test_candidate_exact_set_passes(self):
        entries = [channel_entry(arch, i, risk="candidate") for i, arch in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)]
        ok, problems = sr.verify_channel(entries, "candidate", EXPECTED)
        self.assertTrue(ok, problems)

    def test_missing_arch_fails(self):
        entries = [channel_entry("amd64", 1, risk="candidate")]
        ok, problems = sr.verify_channel(entries, "candidate", EXPECTED)
        self.assertFalse(ok)
        self.assertTrue(any("missing" in p for p in problems))

    def test_duplicate_arch_fails(self):
        entries = [channel_entry("amd64", 1, risk="candidate"), channel_entry("amd64", 2, risk="candidate")]
        ok, problems = sr.verify_channel(entries, "candidate", EXPECTED)
        self.assertFalse(ok)
        self.assertTrue(any("duplicate" in p for p in problems))

    def test_wrong_revision_fails(self):
        entries = [channel_entry(arch, i + 100, risk="candidate") for i, arch in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)]
        ok, problems = sr.verify_channel(entries, "candidate", EXPECTED)
        self.assertFalse(ok)

    def test_wrong_version_fails(self):
        entries = [channel_entry(arch, i, risk="candidate", version="4.0.8") for i, arch in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)]
        ok, problems = sr.verify_channel(entries, "candidate", EXPECTED)
        self.assertFalse(ok)

    def test_unexpected_architecture_fails(self):
        entries = [channel_entry("i386", 9, risk="candidate")]
        ok, problems = sr.verify_channel(entries, "candidate", EXPECTED)
        self.assertFalse(ok)
        self.assertTrue(any("unexpected" in p for p in problems))

    def test_branched_and_other_risks_are_ignored(self):
        entries = [
            channel_entry("amd64", 1, risk="candidate", branch="release-test"),
            channel_entry("amd64", 1, risk="edge"),
            channel_entry("amd64", 1, risk="candidate"),
        ] + [channel_entry(arch, i, risk="candidate") for i, arch in enumerate(("arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=2)]
        # branched candidate and edge entries must not count toward the
        # candidate build set; the unbranched candidate entry is the only one.
        by_arch = {}
        for arch, rev, ver in sr.channel_entries(entries, "latest", "candidate"):
            by_arch.setdefault(arch, []).append(rev)
        self.assertEqual(by_arch["amd64"], [1])

    def test_candidate_at_version_recovery(self):
        expected = {a: i for i, a in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)}
        info = self.build_map([channel_entry(arch, i, risk="candidate") for i, arch in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)])
        self.assertEqual(sr.candidate_at_version(info, sr.EXPECTED_ARCHS, VERSION), expected)
        # missing entry -> no recovery
        info = self.build_map([channel_entry("amd64", 1, risk="candidate")])
        self.assertIsNone(sr.candidate_at_version(info, sr.EXPECTED_ARCHS, VERSION))

    def test_snapshot_stable(self):
        entries = [channel_entry("amd64", 10, risk="stable", version="4.0.8")]
        snap = sr.snapshot_stable(entries)
        self.assertEqual(snap["amd64"], {"revision": 10, "version": "4.0.8"})


class Commands(unittest.TestCase):
    def test_release_command_is_positional(self):
        self.assertEqual(sr.release_command(42), ["snapcraft", "release", "nift", "42", "latest/candidate"])

    def test_promote_command_uses_yes(self):
        self.assertEqual(
            sr.promote_command(),
            ["snapcraft", "promote", "nift", "--from-channel=latest/candidate", "--to-channel=latest/stable", "--yes"],
        )

    def test_rollback_commands_are_positional_and_sorted(self):
        previous = {"arm64": {"revision": 7, "version": "4.0.8"}, "amd64": {"revision": 5, "version": "4.0.8"}}
        cmds = sr.build_rollback_commands(previous)
        self.assertEqual(
            cmds,
            ["snapcraft release nift 5 latest/stable", "snapcraft release nift 7 latest/stable"],
        )

    def test_credentials_guard(self):
        saved = os.environ.get("SNAPCRAFT_STORE_CREDENTIALS")
        try:
            os.environ.pop("SNAPCRAFT_STORE_CREDENTIALS", None)
            self.assertFalse(sr.credentials_present())
            os.environ["SNAPCRAFT_STORE_CREDENTIALS"] = "secret"
            self.assertTrue(sr.credentials_present())
        finally:
            if saved is None:
                os.environ.pop("SNAPCRAFT_STORE_CREDENTIALS", None)
            else:
                os.environ["SNAPCRAFT_STORE_CREDENTIALS"] = saved


class CoordinatorDryRun(unittest.TestCase):
    """Runs main() in --dry-run against a fixture channel map (no network)."""

    def staged_fetch(self):
        # Times relative to the real clock so the run is date-independent.
        base = datetime.datetime.now(datetime.timezone.utc)
        start = base - datetime.timedelta(minutes=30)
        created = base - datetime.timedelta(minutes=10)

        def rev(num, arch):
            return revision(num, arch, version=VERSION, created=created.strftime("%Y-%m-%dT%H:%M:%SZ"))

        six = [rev(i, arch) for i, arch in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)]
        candidate_map = [channel_entry(arch, i, risk="candidate") for i, arch in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)]
        stable_map = [channel_entry(arch, i, risk="stable") for i, arch in enumerate(("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x"), start=1)]
        states = [
            {"channel-map": [], "revisions": six},                                  # initial + wait
            {"channel-map": candidate_map, "revisions": six},                       # after releases to candidate
            {"channel-map": stable_map, "revisions": six},                          # after promote
        ]
        index = {"n": 0}

        def fetch():
            idx = index["n"]
            index["n"] += 1
            if idx <= 1:
                return states[0]  # initial + the wait-loop probe
            if idx == 2:
                return states[1]  # after releases to candidate
            return states[2]      # after promote

        return start.strftime("%Y-%m-%dT%H:%M:%SZ"), fetch

    def run_main(self, env, fetch):
        saved_info = sr.fetch_info
        saved_env = dict(os.environ)
        try:
            sr.fetch_info = fetch
            os.environ.update(env)
            buf = io.StringIO()
            stdout, sys.stdout = sys.stdout, buf
            try:
                code = sr.main(["--dry-run"])
            finally:
                sys.stdout = stdout
            return code, buf.getvalue()
        finally:
            sr.fetch_info = saved_info
            os.environ.clear()
            os.environ.update(saved_env)

    def test_successful_coordination(self):
        start, fetch = self.staged_fetch()
        code, out = self.run_main(
            {
                "NIFT_SNAP_VERSION": VERSION,
                "NIFT_SNAP_START": start,
                "SNAPCRAFT_STORE_CREDENTIALS": "secret",
            },
            fetch,
        )
        self.assertEqual(code, 0, out)
        self.assertIn("DRY-RUN: snapcraft release nift 1 latest/candidate", out)
        self.assertIn("DRY-RUN: snapcraft promote nift --from-channel=latest/candidate --to-channel=latest/stable --yes", out)
        self.assertIn("Stable verified", out)

    def test_missing_credentials_fails_closed_before_network(self):
        # Non-dry-run: the credential guard must stop the run before any Store
        # query is attempted.
        def boom():
            raise AssertionError("fetch_info must not be called without credentials")
        saved_info = sr.fetch_info
        saved_env = dict(os.environ)
        try:
            sr.fetch_info = boom
            os.environ.update({"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_START": START})
            self.assertEqual(sr.main([]), 2)
        finally:
            sr.fetch_info = saved_info
            os.environ.clear()
            os.environ.update(saved_env)

    def test_store_query_failure_fails_closed(self):
        def boom():
            raise OSError("no route")
        saved = sr.fetch_info
        saved_env = dict(os.environ)
        try:
            sr.fetch_info = boom
            os.environ.update({"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_START": START, "SNAPCRAFT_STORE_CREDENTIALS": "secret"})
            self.assertEqual(sr.main(["--dry-run"]), 1)
        finally:
            sr.fetch_info = saved
            os.environ.clear()
            os.environ.update(saved_env)


class WorkflowStructure(unittest.TestCase):
    def load(self, path):
        with open(os.path.join(REPO, path), encoding="utf-8") as f:
            return f.read()

    def test_no_direct_tag_release_to_stable(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertNotIn("release: stable", text)

    def test_release_coordination_uses_candidate_promotion(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertIn("release-coordination", text)
        # The coordinator job runs the release script; the promote/release
        # command contract lives in that script and is asserted verbatim.
        self.assertIn("packaging/snap_release.py", text)
        script = self.load("packaging/snap_release.py")
        self.assertIn("--from-channel=latest/candidate", script)
        self.assertIn("--to-channel=latest/stable", script)
        self.assertIn("--yes", script)
        self.assertEqual(
            sr.promote_command(),
            ["snapcraft", "promote", "nift", "--from-channel=latest/candidate", "--to-channel=latest/stable", "--yes"],
        )

    def test_concurrency_group_serializes_release_transaction(self):
        self.assertIn("nift-snap-release-transaction", self.load(".github/workflows/snap.yml"))
        self.assertIn("cancel-in-progress: false", self.load(".github/workflows/snap.yml"))

    def test_store_credentials_only_on_publishing_jobs(self):
        text = self.load(".github/workflows/snap.yml")
        # Build-only jobs must not hold the Store credential; only
        # publish-edge and release-coordination may.
        self.assertEqual(text.count("SNAPCRAFT_STORE_CREDENTIALS:"), 2)
        build_section = text.split("publish-edge")[0]
        self.assertNotIn("SNAPCRAFT_STORE_CREDENTIALS", build_section)

    def test_manual_edge_publication_isolated(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertIn("publish-edge", text)
        self.assertIn("release=edge", text)
        self.assertIn("workflow_dispatch", text)

    def test_release_workflow_calls_snap_via_workflow_call(self):
        self.assertIn("uses: ./.github/workflows/snap.yml", self.load(".github/workflows/release.yml"))


if __name__ == "__main__":
    unittest.main(verbosity=2)