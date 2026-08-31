#!/usr/bin/env python3
"""Offline contract tests for the nift Snap publication coordinator.

Covers: the exact six-architecture set; coordination through the connected
build-service latest/edge revisions (no timestamp-window selection); the real v2
channel-map schema (architecture inside channel, created-at spelling); rejection
of an API error-list; polling while an edge architecture lags; legacy i386
ignored but reported; exact edge revision numbers carried through candidate and
stable; candidate staging and confinement smoke ordered before promotion; the
exact promote --yes invocation; unbranched channel filtering; positional
rollback syntax; no duplicate publisher targeting unbranched edge; literal YAML
shell expressions rejected; an immutable Snapcraft pin; and missing publishing
credentials failing rather than skipping. No network access and no Store
operations are performed.
"""

import datetime
import io
import json
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "packaging"))
sys.path.insert(0, os.path.dirname(__file__))

import snap_release as sr

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
VERSION = "4.0.9"
ARCHS = ("amd64", "arm64", "armhf", "ppc64el", "riscv64", "s390x")


def load_fixture():
    path = os.path.join(os.path.dirname(__file__), "fixtures", "snap_channel_map_real.json")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def channel_entry(arch, rev, version, risk, track="latest", branch=None):
    return {
        "channel": {"architecture": arch, "name": risk, "risk": risk, "track": track, "branch": branch},
        "created-at": "2026-08-28T10:00:00Z",
        "revision": rev,
        "version": version,
    }


def edge_map_at(version, revisions):
    """{arch: revision} -> a latest/edge channel-map for all six architectures."""
    entries = [channel_entry(arch, revisions[arch], version, risk="edge") for arch in ARCHS]
    return {"channel-map": entries}


def candidate_map(revisions, version=VERSION):
    return {"channel-map": [channel_entry(arch, revisions[arch], version, risk="candidate") for arch in ARCHS]}


def stable_map(revisions, version):
    return {"channel-map": [channel_entry(arch, revisions[arch], version, risk="stable") for arch in ARCHS]}


def revision_numbers(offset=0):
    return {arch: 100 + offset + i for i, arch in enumerate(ARCHS)}


class PlatformsContract(unittest.TestCase):
    def test_declared_platforms_match_contract(self):
        with open(os.path.join(REPO, "snap", "snapcraft.yaml"), encoding="utf-8") as f:
            archs = sr.parse_platforms(f.read())
        self.assertEqual(archs, sr.EXPECTED_ARCHS)


class RealChannelMapFixture(unittest.TestCase):
    """The production parser must understand the captured real response shape."""

    def test_architecture_is_read_from_channel(self):
        data = load_fixture()
        entries = sr.channel_entries(data["channel-map"], "latest", "edge")
        by_arch = {arch: (rev, ver) for arch, rev, ver in entries}
        self.assertEqual(by_arch["amd64"], (684, "4.0.9"))
        self.assertEqual(by_arch["s390x"], (683, "4.0.8"))
        # no architecture may be read as None
        self.assertNotIn(None, by_arch)

    def test_fixture_uses_real_created_at_spelling(self):
        entry = load_fixture()["channel-map"][0]
        self.assertIn("created-at", entry)
        self.assertNotIn("created_at", entry)

    def test_edge_selection_reports_lagging_architecture(self):
        data = load_fixture()
        selected, waiting, duplicates, legacy = sr.select_edge_revisions(data["channel-map"], "4.0.9", sr.EXPECTED_ARCHS)
        self.assertIn("s390x", waiting)
        self.assertNotIn("s390x", selected)
        self.assertEqual(len(selected), 5)
        self.assertEqual(duplicates, [])
        self.assertIn(("i386", 12, "3.0.3"), legacy)

    def test_completion_when_all_edge_revisions_reach_version(self):
        data = load_fixture()
        # Move s390x to 4.0.9 to represent the completed build set.
        for entry in data["channel-map"]:
            if entry["channel"]["architecture"] == "s390x" and entry["channel"]["risk"] == "edge":
                entry["version"] = "4.0.9"
        selected, waiting, duplicates, legacy = sr.select_edge_revisions(data["channel-map"], "4.0.9", sr.EXPECTED_ARCHS)
        self.assertEqual(set(selected), sr.EXPECTED_ARCHS)
        self.assertEqual(waiting, [])
        self.assertEqual(duplicates, [])
        self.assertTrue(any(a == "i386" for a, _, _ in legacy))

    def test_snapshot_stable_ignores_legacy_i386(self):
        data = load_fixture()
        snap = sr.snapshot_stable(data["channel-map"], sr.EXPECTED_ARCHS)
        self.assertEqual(set(snap), sr.EXPECTED_ARCHS)
        self.assertNotIn("i386", snap)
        legacy = sr.legacy_entries(data["channel-map"], "stable", sr.EXPECTED_ARCHS)
        self.assertIn(("i386", 11, "3.0.3"), legacy)

    def test_stable_verification_ignores_legacy_i386(self):
        data = load_fixture()
        expected = {arch: {"revision": rev, "version": "4.0.8"}
                    for arch, rev in zip(ARCHS, (679, 680, 681, 682, 683, 684))}
        ok, problems = sr.verify_channel(data["channel-map"], "stable", expected)
        self.assertTrue(ok, problems)
        self.assertTrue(any(a == "i386" for a, _, _ in sr.legacy_entries(data["channel-map"], "stable", sr.EXPECTED_ARCHS)))


class EdgeSelection(unittest.TestCase):
    def test_all_six_selected_at_version(self):
        data = edge_map_at(VERSION, revision_numbers())
        selected, waiting, duplicates, legacy = sr.select_edge_revisions(data["channel-map"], VERSION, sr.EXPECTED_ARCHS)
        self.assertEqual(set(selected), sr.EXPECTED_ARCHS)
        self.assertEqual(waiting, [])
        self.assertEqual(duplicates, [])
        self.assertEqual(legacy, [])

    def test_missing_architecture_is_waiting(self):
        data = edge_map_at(VERSION, revision_numbers())
        data["channel-map"] = [e for e in data["channel-map"] if e["channel"]["architecture"] != "s390x"]
        _, waiting, _, _ = sr.select_edge_revisions(data["channel-map"], VERSION, sr.EXPECTED_ARCHS)
        self.assertIn("s390x", waiting)

    def test_older_version_is_waiting(self):
        data = edge_map_at(VERSION, revision_numbers())
        for entry in data["channel-map"]:
            if entry["channel"]["architecture"] == "riscv64":
                entry["version"] = "4.0.8"
        _, waiting, _, _ = sr.select_edge_revisions(data["channel-map"], VERSION, sr.EXPECTED_ARCHS)
        self.assertIn("riscv64", waiting)

    def test_duplicate_edge_entries_fail_closed(self):
        data = edge_map_at(VERSION, revision_numbers())
        data["channel-map"].append(channel_entry("amd64", 999, VERSION, risk="edge"))
        _, _, duplicates, _ = sr.select_edge_revisions(data["channel-map"], VERSION, sr.EXPECTED_ARCHS)
        self.assertEqual(duplicates, ["amd64"])


class ErrorListRejection(unittest.TestCase):
    def test_api_error_list_raises(self):
        payload = {"error-list": [{"code": "api-error", "message": "'revisions' is not one of [...]"}]}
        with self.assertRaises(RuntimeError):
            sr.parse_channel_map_response(payload)

    def test_valid_payload_returns_channel_map(self):
        payload = {"channel-map": [channel_entry("amd64", 1, VERSION, risk="edge")]}
        self.assertEqual(len(sr.parse_channel_map_response(payload)), 1)


class ChannelVerification(unittest.TestCase):
    def build_map(self, entries):
        return {"channel-map": entries}

    def test_candidate_exact_set_passes(self):
        revs = revision_numbers()
        entries = [channel_entry(arch, revs[arch], VERSION, risk="candidate") for arch in ARCHS]
        expected = {arch: {"revision": revs[arch], "version": VERSION} for arch in ARCHS}
        ok, problems = sr.verify_channel(entries, "candidate", expected)
        self.assertTrue(ok, problems)

    def test_missing_arch_fails(self):
        entries = [channel_entry("amd64", 1, VERSION, risk="candidate")]
        ok, problems = sr.verify_channel(entries, "candidate", {a: {"revision": 1, "version": VERSION} for a in ARCHS})
        self.assertFalse(ok)
        self.assertTrue(any("missing" in p for p in problems))

    def test_duplicate_arch_fails(self):
        entries = [channel_entry("amd64", 1, VERSION, risk="candidate"), channel_entry("amd64", 2, VERSION, risk="candidate")]
        ok, problems = sr.verify_channel(entries, "candidate", {"amd64": {"revision": 1, "version": VERSION}})
        self.assertFalse(ok)
        self.assertTrue(any("duplicate" in p for p in problems))

    def test_wrong_revision_and_version_fail(self):
        expected = {"amd64": {"revision": 1, "version": VERSION}}
        self.assertFalse(sr.verify_channel([channel_entry("amd64", 2, VERSION, risk="candidate")], "candidate", expected)[0])
        self.assertFalse(sr.verify_channel([channel_entry("amd64", 1, "4.0.8", risk="candidate")], "candidate", expected)[0])

    def test_branched_and_other_risks_ignored(self):
        entries = [
            channel_entry("amd64", 1, VERSION, risk="candidate", branch="release-test"),
            channel_entry("amd64", 1, VERSION, risk="edge"),
            channel_entry("amd64", 1, VERSION, risk="candidate"),
        ]
        expected = {"amd64": {"revision": 1, "version": VERSION}}
        ok, problems = sr.verify_channel(entries, "candidate", expected)
        self.assertTrue(ok, problems)

    def test_legacy_i386_does_not_fail_stable(self):
        entries = stable_map(revision_numbers(), VERSION)["channel-map"] + [
            channel_entry("i386", 11, "3.0.3", risk="stable")]
        expected = {arch: {"revision": revision_numbers()[arch], "version": VERSION} for arch in ARCHS}
        ok, problems = sr.verify_channel(entries, "stable", expected)
        self.assertTrue(ok, problems)
        self.assertTrue(any(a == "i386" for a, _, _ in sr.legacy_entries(entries, "stable", sr.EXPECTED_ARCHS)))

    def test_candidate_at_version_ignores_legacy(self):
        revs = revision_numbers()
        entries = [channel_entry(arch, revs[arch], VERSION, risk="candidate") for arch in ARCHS] + [
            channel_entry("i386", 11, "3.0.3", risk="candidate")]
        data = {"channel-map": entries}
        self.assertEqual(sr.candidate_at_version(data["channel-map"], sr.EXPECTED_ARCHS, VERSION), revs)
        data = {"channel-map": [channel_entry("amd64", 1, VERSION, risk="candidate")]}
        self.assertIsNone(sr.candidate_at_version(data["channel-map"], sr.EXPECTED_ARCHS, VERSION))


class SnapshotAndRollback(unittest.TestCase):
    def test_snapshot_stable_positional_rollback(self):
        revs = revision_numbers()
        entries = stable_map(revs, "4.0.8")["channel-map"] + [channel_entry("i386", 11, "3.0.3", risk="stable")]
        snap = sr.snapshot_stable(entries, sr.EXPECTED_ARCHS)
        self.assertEqual(set(snap), sr.EXPECTED_ARCHS)
        cmds = sr.build_rollback_commands(snap)
        self.assertEqual(cmds[0], "snapcraft release nift 100 latest/stable")  # amd64 revision 100
        self.assertTrue(all(cmd.startswith("snapcraft release nift ") for cmd in cmds))
        self.assertTrue(all(cmd.endswith(" latest/stable") for cmd in cmds))


class Commands(unittest.TestCase):
    def test_release_command_is_positional(self):
        self.assertEqual(sr.release_command(42), ["snapcraft", "release", "nift", "42", "latest/candidate"])

    def test_promote_command_uses_yes(self):
        self.assertEqual(
            sr.promote_command(),
            ["snapcraft", "promote", "nift", "--from-channel=latest/candidate", "--to-channel=latest/stable", "--yes"],
        )

    def test_smoke_command(self):
        self.assertEqual(
            sr.smoke_command(VERSION, 684),
            ["bash", "packaging/snap-candidate-smoke.sh", VERSION, "684"],
        )


class CoordinatorDryRun(unittest.TestCase):
    """Runs main() in --dry-run against stateful channel-map fixtures (no network)."""

    def staged_fetch(self):
        revs = revision_numbers()
        waiting = edge_map_at(VERSION, revs)
        # One architecture still lags so the poll loop is exercised.
        for entry in waiting["channel-map"]:
            if entry["channel"]["architecture"] == "s390x":
                entry["version"] = "4.0.8"
        complete = edge_map_at(VERSION, revs)
        candidate = candidate_map(revs)
        stable = stable_map(revs, VERSION)
        states = [waiting, waiting, complete, candidate, stable]
        index = {"n": 0}

        def fetch():
            state = states[min(index["n"], len(states) - 1)]
            index["n"] += 1
            return state["channel-map"]

        return fetch

    def run_main(self, env, fetch):
        saved_fetch = sr.fetch_channel_map
        saved_env = dict(os.environ)
        try:
            sr.fetch_channel_map = fetch
            os.environ.update(env)
            buf = io.StringIO()
            stdout, sys.stdout = sys.stdout, buf
            stderr, sys.stderr = sys.stderr, buf
            try:
                code = sr.main(["--dry-run"])
            finally:
                sys.stdout = stdout
                sys.stderr = stderr
            return code, buf.getvalue()
        finally:
            sr.fetch_channel_map = saved_fetch
            os.environ.clear()
            os.environ.update(saved_env)

    def test_successful_coordination_via_edge(self):
        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_WAIT": "1", "NIFT_SNAP_POLL": "0", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            self.staged_fetch(),
        )
        self.assertEqual(code, 0, out)
        self.assertIn("DRY-RUN: snapcraft release nift 100 latest/candidate", out)
        self.assertIn("DRY-RUN: bash packaging/snap-candidate-smoke.sh", out)
        self.assertIn("DRY-RUN: snapcraft promote nift --from-channel=latest/candidate --to-channel=latest/stable --yes", out)
        self.assertIn("Stable verified", out)

    def test_missing_credentials_fail_closed_before_network(self):
        def boom():
            raise AssertionError("fetch must not be called without credentials")
        saved_fetch = sr.fetch_channel_map
        saved_env = dict(os.environ)
        try:
            sr.fetch_channel_map = boom
            os.environ.update({"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_WAIT": "1", "NIFT_SNAP_POLL": "0"})
            self.assertEqual(sr.main([]), 2)
        finally:
            sr.fetch_channel_map = saved_fetch
            os.environ.clear()
            os.environ.update(saved_env)

    def test_store_query_failure_fails_closed(self):
        def boom():
            raise OSError("no route")
        saved_fetch = sr.fetch_channel_map
        saved_env = dict(os.environ)
        try:
            sr.fetch_channel_map = boom
            os.environ.update({"NIFT_SNAP_VERSION": VERSION, "SNAPCRAFT_STORE_CREDENTIALS": "secret"})
            self.assertEqual(sr.main(["--dry-run"]), 1)
        finally:
            sr.fetch_channel_map = saved_fetch
            os.environ.clear()
            os.environ.update(saved_env)

    def test_timeout_while_architecture_lags_fails_closed(self):
        revs = revision_numbers()
        lagging = edge_map_at(VERSION, revs)
        for entry in lagging["channel-map"]:
            if entry["channel"]["architecture"] == "riscv64":
                entry["version"] = "4.0.8"

        def always_lagging():
            return lagging["channel-map"]

        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_WAIT": "1", "NIFT_SNAP_POLL": "0", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            always_lagging,
        )
        self.assertEqual(code, 1)
        self.assertIn("timed out", out)


class WorkflowStructure(unittest.TestCase):
    def load(self, path):
        with open(os.path.join(REPO, path), encoding="utf-8") as f:
            return f.read()

    def env_block_values(self, yaml_text):
        """Values inside top-level `env:` blocks (not shell `run:` bodies)."""
        lines = yaml_text.splitlines()
        values = []
        i = 0
        n = len(lines)
        while i < n:
            if lines[i].rstrip() == "env:":
                j = i + 1
                indent = None
                while j < n:
                    nxt = lines[j]
                    stripped = nxt.lstrip()
                    if not stripped:
                        j += 1
                        continue
                    lead = len(nxt) - len(stripped)
                    if indent is None:
                        indent = lead
                    if lead <= indent:
                        break
                    m = re.match(r"^\s*[A-Za-z0-9_]+:\s*(.*)$", nxt)
                    if m:
                        values.append(m.group(1))
                    j += 1
                i = j
            else:
                i += 1
        return values

    def test_no_direct_tag_release_to_stable(self):
        self.assertNotIn("release: stable", self.load(".github/workflows/snap.yml"))

    def test_literal_shell_expressions_rejected_in_yaml_env(self):
        text = self.load(".github/workflows/snap.yml")
        # Shell expansion is never valid inside YAML env: values (they are not
        # evaluated by a shell). GitHub `${{ ... }}` expressions are allowed.
        self.assertNotIn("$(date", text)
        for value in self.env_block_values(text):
            self.assertNotIn("$(", value)
            if "${" in value and "${{" not in value:
                self.fail("shell expansion in YAML env value: " + value)
        # The dynamic version is resolved inside a shell step and exported.
        self.assertIn('"NIFT_SNAP_VERSION=${GITHUB_REF_NAME#v}" >> "$GITHUB_ENV"', text)

    def test_dynamic_values_resolved_via_github_env(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertIn(">> \"$GITHUB_ENV\"", text)

    def test_release_coordination_runs_the_coordinator(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertIn("release-coordination", text)
        self.assertIn("packaging/snap_release.py", text)
        script = self.load("packaging/snap_release.py")
        self.assertIn("--from-channel=latest/candidate", script)
        self.assertIn("--to-channel=latest/stable", script)
        self.assertIn("--yes", script)

    def test_concurrency_group_serializes_release_transaction(self):
        self.assertIn("nift-snap-release-transaction", self.load(".github/workflows/snap.yml"))
        self.assertIn("cancel-in-progress: false", self.load(".github/workflows/snap.yml"))

    def test_store_credentials_only_on_publishing_job(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertEqual(text.count("SNAPCRAFT_STORE_CREDENTIALS:"), 1)
        build_section = text.split("release-coordination")[0]
        self.assertNotIn("SNAPCRAFT_STORE_CREDENTIALS", build_section)

    def test_no_duplicate_publisher_to_unbranched_edge(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertNotIn("publish-edge", text)
        self.assertNotIn("release=edge", text)
        self.assertNotIn("--release=edge", text)

    def test_snapcraft_pin_is_immutable(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertIn("SNAPCRAFT_SNAP_REVISION", text)
        self.assertIn("--revision=", text)
        self.assertNotIn("${SNAPCRAFT_CHANNEL:-latest/stable}", text)

    def test_candidate_smoke_ordered_before_promotion(self):
        script = self.load("packaging/snap_release.py")
        smoke = script.index("snap-candidate-smoke.sh")
        promote = script.index("promote_command()")
        self.assertLess(smoke, promote)

    def test_no_timestamp_window_selection(self):
        script = self.load("packaging/snap_release.py")
        self.assertNotIn("created_at", script)
        self.assertNotIn("NIFT_SNAP_START", script)
        self.assertNotIn("tolerance", script)

    def test_fetch_uses_only_valid_field(self):
        script = self.load("packaging/snap_release.py")
        self.assertIn("fields=channel-map", script)
        self.assertNotIn("channel-map,revisions", script)

    def test_no_credential_skip_gate_on_publication(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertNotIn("SNAPCRAFT_STORE_CREDENTIALS != ''", text)

    def test_release_workflow_calls_snap_via_workflow_call(self):
        self.assertIn("uses: ./.github/workflows/snap.yml", self.load(".github/workflows/release.yml"))


if __name__ == "__main__":
    unittest.main(verbosity=2)