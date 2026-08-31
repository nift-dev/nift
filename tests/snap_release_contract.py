#!/usr/bin/env python3
"""Offline contract tests for the nift Snap publication coordinator.

Covers: the exact six-architecture set; coordination through the connected
build-service latest/edge revisions (no timestamp-window selection); the real v2
channel-map schema (architecture inside channel, released-at spelling, branch
omitted when unbranched); the required fields=channel-map,revision,version
request; rejection of an API error-list and of entries lacking revision/version;
entry validation; polling while an edge architecture lags; legacy i386 ignored
    and reported in edge/stable but fail-closed in candidate; exact edge revision
    numbers carried through candidate and stable; a complete rollback snapshot
    before any candidate mutation; candidate staging and confinement smoke ordered
    before any stable mutation; the exact per-revision stable release invocation
    (whole-channel promote never used); positional rollback syntax; no
    duplicate publisher targeting unbranched edge; literal YAML shell expressions
rejected; the actual immutable Snapcraft pin with post-install assertions; sudo
candidate install/cleanup; and missing publishing credentials failing rather
than skipping. No network access and no Store operations are performed.
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
    channel = {
        "architecture": arch,
        "name": risk,
        "released-at": "2026-08-28T10:00:00Z",
        "risk": risk,
        "track": track,
    }
    if branch is not None:
        channel["branch"] = branch
    return {"channel": channel, "revision": rev, "version": version}


def edge_map_at(version, revisions):
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
    def test_architecture_read_from_channel_and_revision_version_top_level(self):
        data = load_fixture()
        entries = sr.channel_entries(data["channel-map"], "latest", "edge")
        by_arch = {arch: (rev, ver) for arch, rev, ver in entries}
        self.assertEqual(by_arch["amd64"], (684, "4.0.9"))
        self.assertEqual(by_arch["s390x"], (683, "4.0.8"))
        self.assertNotIn(None, by_arch)

    def test_fixture_matches_real_schema(self):
        entry = load_fixture()["channel-map"][0]
        self.assertIn("released-at", entry["channel"])
        self.assertNotIn("branch", entry["channel"])
        self.assertIn("revision", entry)
        self.assertIn("version", entry)

    def test_edge_selection_reports_lagging_architecture(self):
        data = load_fixture()
        selected, waiting, duplicates, legacy, malformed = sr.select_edge_revisions(
            data["channel-map"], "4.0.9", sr.EXPECTED_ARCHS)
        self.assertIn("s390x", waiting)
        self.assertNotIn("s390x", selected)
        self.assertEqual(len(selected), 5)
        self.assertEqual(duplicates, [])
        self.assertEqual(malformed, [])
        self.assertIn(("i386", 12, "3.0.3"), legacy)

    def test_completion_when_all_edge_revisions_reach_version(self):
        data = load_fixture()
        for entry in data["channel-map"]:
            if entry["channel"]["architecture"] == "s390x" and entry["channel"]["risk"] == "edge":
                entry["version"] = "4.0.9"
        selected, waiting, duplicates, legacy, malformed = sr.select_edge_revisions(
            data["channel-map"], "4.0.9", sr.EXPECTED_ARCHS)
        self.assertEqual(set(selected), sr.EXPECTED_ARCHS)
        self.assertEqual(waiting, [])
        self.assertEqual(duplicates, [])
        self.assertEqual(malformed, [])
        self.assertTrue(any(a == "i386" for a, _, _ in legacy))

    def test_snapshot_stable_ignores_legacy_i386(self):
        data = load_fixture()
        snap = sr.snapshot_stable(data["channel-map"], sr.EXPECTED_ARCHS)
        self.assertEqual(set(snap), sr.EXPECTED_ARCHS)
        self.assertNotIn("i386", snap)

    def test_stable_verification_ignores_legacy_i386(self):
        data = load_fixture()
        expected = {arch: {"revision": rev, "version": "4.0.8"}
                    for arch, rev in zip(ARCHS, (679, 680, 681, 682, 683, 684))}
        ok, problems = sr.verify_channel(data["channel-map"], "stable", expected)
        self.assertTrue(ok, problems)


class EdgeSelection(unittest.TestCase):
    def test_all_six_selected_at_version(self):
        data = edge_map_at(VERSION, revision_numbers())
        selected, waiting, duplicates, legacy, malformed = sr.select_edge_revisions(
            data["channel-map"], VERSION, sr.EXPECTED_ARCHS)
        self.assertEqual(set(selected), sr.EXPECTED_ARCHS)
        self.assertEqual(waiting, [])
        self.assertEqual(duplicates, [])
        self.assertEqual(legacy, [])
        self.assertEqual(malformed, [])

    def test_missing_architecture_is_waiting(self):
        data = edge_map_at(VERSION, revision_numbers())
        data["channel-map"] = [e for e in data["channel-map"] if e["channel"]["architecture"] != "s390x"]
        _, waiting, _, _, _ = sr.select_edge_revisions(data["channel-map"], VERSION, sr.EXPECTED_ARCHS)
        self.assertIn("s390x", waiting)

    def test_older_version_is_waiting(self):
        data = edge_map_at(VERSION, revision_numbers())
        for entry in data["channel-map"]:
            if entry["channel"]["architecture"] == "riscv64":
                entry["version"] = "4.0.8"
        _, waiting, _, _, _ = sr.select_edge_revisions(data["channel-map"], VERSION, sr.EXPECTED_ARCHS)
        self.assertIn("riscv64", waiting)

    def test_duplicate_edge_entries_fail_closed(self):
        data = edge_map_at(VERSION, revision_numbers())
        data["channel-map"].append(channel_entry("amd64", 999, VERSION, risk="edge"))
        _, _, duplicates, _, _ = sr.select_edge_revisions(data["channel-map"], VERSION, sr.EXPECTED_ARCHS)
        self.assertEqual(duplicates, ["amd64"])

    def test_fields_channel_map_only_response_is_malformed_not_waiting(self):
        # A fields=channel-map-only response has no top-level revision/version.
        entries = []
        for arch in ARCHS:
            entries.append({"channel": {"architecture": arch, "name": "edge", "risk": "edge", "track": "latest"}})
        _, waiting, _, _, malformed = sr.select_edge_revisions(entries, VERSION, sr.EXPECTED_ARCHS)
        self.assertEqual(waiting, [])
        self.assertEqual(len(malformed), len(ARCHS))
        self.assertTrue(all("revision" in problem or "version" in problem for _, problem in malformed))


class EntryValidation(unittest.TestCase):
    def test_valid_entry_passes(self):
        self.assertIsNone(sr.entry_problem(channel_entry("amd64", 1, VERSION, risk="edge")))

    def test_missing_revision_fails(self):
        entry = channel_entry("amd64", 1, VERSION, risk="edge")
        del entry["revision"]
        self.assertIsNotNone(sr.entry_problem(entry))

    def test_non_positive_revision_fails(self):
        self.assertIsNotNone(sr.entry_problem(channel_entry("amd64", 0, VERSION, risk="edge")))
        self.assertIsNotNone(sr.entry_problem(channel_entry("amd64", "x", VERSION, risk="edge")))

    def test_empty_version_fails(self):
        self.assertIsNotNone(sr.entry_problem(channel_entry("amd64", 1, "", risk="edge")))

    def test_missing_architecture_fails(self):
        entry = channel_entry("amd64", 1, VERSION, risk="edge")
        del entry["channel"]["architecture"]
        self.assertIsNotNone(sr.entry_problem(entry))

    def test_missing_track_or_risk_fails(self):
        entry = channel_entry("amd64", 1, VERSION, risk="edge")
        del entry["channel"]["track"]
        self.assertIsNotNone(sr.entry_problem(entry))
        entry = channel_entry("amd64", 1, VERSION, risk="edge")
        del entry["channel"]["risk"]
        self.assertIsNotNone(sr.entry_problem(entry))


class ErrorListRejection(unittest.TestCase):
    def test_api_error_list_raises(self):
        payload = {"error-list": [{"code": "api-error", "message": "'revisions' is not one of [...]"}]}
        with self.assertRaises(RuntimeError):
            sr.parse_channel_map_response(payload)

    def test_valid_payload_returns_channel_map(self):
        payload = {"channel-map": [channel_entry("amd64", 1, VERSION, risk="edge")]}
        self.assertEqual(len(sr.parse_channel_map_response(payload)), 1)


class ChannelVerification(unittest.TestCase):
    def test_candidate_exact_set_passes(self):
        revs = revision_numbers()
        entries = candidate_map(revs)["channel-map"]
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
        ok, problems = sr.verify_channel(entries, "candidate", {"amd64": {"revision": 1, "version": VERSION}})
        self.assertTrue(ok, problems)


class CandidateStrict(unittest.TestCase):
    def revs(self):
        return revision_numbers()

    def expected(self):
        revs = self.revs()
        return {arch: {"revision": revs[arch], "version": VERSION} for arch in ARCHS}

    def test_exact_six_pass(self):
        ok, problems = sr.verify_candidate_strict(candidate_map(self.revs())["channel-map"], self.expected(), sr.EXPECTED_ARCHS)
        self.assertTrue(ok, problems)

    def test_legacy_candidate_entry_fails_closed(self):
        entries = candidate_map(self.revs())["channel-map"] + [
            channel_entry("i386", 11, "3.0.3", risk="candidate")]
        ok, problems = sr.verify_candidate_strict(entries, self.expected(), sr.EXPECTED_ARCHS)
        self.assertFalse(ok)
        self.assertTrue(any("unsupported candidate entry" in p for p in problems))

    def test_missing_arch_fails(self):
        entries = [channel_entry("amd64", self.revs()["amd64"], VERSION, risk="candidate")]
        ok, problems = sr.verify_candidate_strict(entries, self.expected(), sr.EXPECTED_ARCHS)
        self.assertFalse(ok)
        self.assertTrue(any("missing candidate entry" in p for p in problems))

    def test_duplicate_fails(self):
        entries = candidate_map(self.revs())["channel-map"] + [
            channel_entry("amd64", 999, VERSION, risk="candidate")]
        ok, problems = sr.verify_candidate_strict(entries, self.expected(), sr.EXPECTED_ARCHS)
        self.assertFalse(ok)
        self.assertTrue(any("duplicate candidate entries" in p for p in problems))

    def test_wrong_version_fails(self):
        entries = [channel_entry(arch, self.revs()[arch], "4.0.8", risk="candidate") for arch in ARCHS]
        ok, problems = sr.verify_candidate_strict(entries, self.expected(), sr.EXPECTED_ARCHS)
        self.assertFalse(ok)


class RollbackCompleteness(unittest.TestCase):
    def test_complete_snapshot_passes(self):
        entries = stable_map(revision_numbers(), "4.0.8")["channel-map"]
        self.assertEqual(sr.stable_snapshot_problems(entries, sr.EXPECTED_ARCHS), [])

    def test_missing_rollback_architecture_reported(self):
        entries = [channel_entry(arch, 1, "4.0.8", risk="stable") for arch in ARCHS if arch != "s390x"]
        problems = sr.stable_snapshot_problems(entries, sr.EXPECTED_ARCHS)
        self.assertTrue(any("missing" in p and "s390x" in p for p in problems))

    def test_malformed_previous_stable_reported(self):
        entries = stable_map(revision_numbers(), "4.0.8")["channel-map"]
        for entry in entries:
            if entry["channel"]["architecture"] == "amd64":
                del entry["revision"]
        problems = sr.stable_snapshot_problems(entries, sr.EXPECTED_ARCHS)
        self.assertTrue(any("malformed" in p and "amd64" in p for p in problems))

    def test_duplicate_previous_stable_reported(self):
        entries = stable_map(revision_numbers(), "4.0.8")["channel-map"] + [
            channel_entry("amd64", 777, "4.0.8", risk="stable")]
        problems = sr.stable_snapshot_problems(entries, sr.EXPECTED_ARCHS)
        self.assertTrue(any("duplicate" in p and "amd64" in p for p in problems))

    def test_legacy_stable_i386_is_ignored_and_never_in_rollback(self):
        entries = stable_map(revision_numbers(), "4.0.8")["channel-map"] + [
            channel_entry("i386", 11, "3.0.3", risk="stable")]
        self.assertEqual(sr.stable_snapshot_problems(entries, sr.EXPECTED_ARCHS), [])
        snap = sr.snapshot_stable(entries, sr.EXPECTED_ARCHS)
        cmds = sr.build_rollback_commands(snap)
        self.assertEqual(len(cmds), len(ARCHS))
        self.assertFalse(any("i386" in cmd for cmd in cmds))


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

    def test_stable_release_command_is_per_revision(self):
        self.assertEqual(
            sr.stable_release_command(42),
            ["snapcraft", "release", "nift", "42", "latest/stable"],
        )

    def test_smoke_command(self):
        self.assertEqual(
            sr.smoke_command(VERSION, 684),
            ["bash", "packaging/snap-candidate-smoke.sh", VERSION, "684"],
        )


class CoordinatorDryRun(unittest.TestCase):
    """Runs main() in --dry-run against stateful channel-map fixtures (no network)."""

    def build_states(self, candidate_extra=None):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)  # previous stable revisions, 600-605
        prev_stable = stable_map(prev, "4.0.8")
        edge_waiting = edge_map_at(VERSION, revs)
        for entry in edge_waiting["channel-map"]:
            if entry["channel"]["architecture"] == "s390x":
                entry["version"] = "4.0.8"
        edge_complete = edge_map_at(VERSION, revs)
        candidate = candidate_map(revs)
        if candidate_extra:
            candidate["channel-map"].extend(candidate_extra)
        stable_new = stable_map(revs, VERSION)

        def combine(*maps):
            entries = []
            for m in maps:
                entries.extend(m["channel-map"])
            return entries

        s0 = combine(edge_waiting, prev_stable)
        s2 = combine(edge_complete, prev_stable)
        s3 = combine(candidate, prev_stable)
        s4 = combine(candidate, stable_new)
        states = [s0, s0, s2, s3, s4]
        index = {"n": 0}

        def fetch():
            state = states[min(index["n"], len(states) - 1)]
            index["n"] += 1
            return state

        return fetch

    def run_main(self, env, fetch, argv=("--dry-run",)):
        saved_fetch = sr.fetch_channel_map
        saved_env = dict(os.environ)
        try:
            sr.fetch_channel_map = fetch
            os.environ.update(env)
            buf = io.StringIO()
            stdout, sys.stdout = sys.stdout, buf
            stderr, sys.stderr = sys.stderr, buf
            try:
                code = sr.main(list(argv))
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
            self.build_states(),
        )
        self.assertEqual(code, 0, out)
        self.assertIn("DRY-RUN: snapcraft release nift 100 latest/candidate", out)
        self.assertIn("DRY-RUN: bash packaging/snap-candidate-smoke.sh", out)
        self.assertNotIn("DRY-RUN: snapcraft promote", out)
        revs = revision_numbers()
        for arch in ARCHS:
            self.assertIn("DRY-RUN: snapcraft release nift {} latest/stable".format(revs[arch]), out)
        self.assertIn("Stable verified", out)

    def test_legacy_candidate_never_promoted(self):
        extra = [channel_entry("i386", 11, "3.0.3", risk="candidate")]
        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_WAIT": "1", "NIFT_SNAP_POLL": "0", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            self.build_states(candidate_extra=extra),
        )
        self.assertEqual(code, 1)
        self.assertIn("unsupported candidate entry i386", out)
        self.assertNotIn("DRY-RUN: snapcraft promote", out)
        self.assertNotIn("latest/stable", out)

    def test_legacy_i386_ignored_while_six_reach_stable(self):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        prev_stable = stable_map(prev, "4.0.8")
        prev_stable["channel-map"].append(channel_entry("i386", 11, "3.0.3", risk="stable"))
        edge_complete = edge_map_at(VERSION, revs)
        edge_complete["channel-map"].append(channel_entry("i386", 11, "3.0.3", risk="edge"))
        candidate = candidate_map(revs)
        stable_new = stable_map(revs, VERSION)
        stable_new["channel-map"].append(channel_entry("i386", 11, "3.0.3", risk="stable"))

        def combine(*maps):
            entries = []
            for m in maps:
                entries.extend(m["channel-map"])
            return entries

        states = [
            combine(edge_complete, prev_stable),
            combine(edge_complete, prev_stable),
            combine(candidate, prev_stable),
            combine(candidate, stable_new),
        ]
        code, out = self.run_main(self.env(), self.stateful_fetch(states))
        self.assertEqual(code, 0, out)
        self.assertNotIn("DRY-RUN: snapcraft promote", out)
        for arch in ARCHS:
            self.assertIn("DRY-RUN: snapcraft release nift {} latest/stable".format(revs[arch]), out)
        self.assertNotIn("DRY-RUN: snapcraft release nift 11 latest/stable", out)
        self.assertIn("i386", out)  # reported as legacy, never released
        self.assertIn("Stable verified", out)

    def test_incomplete_rollback_snapshot_refuses_promotion(self):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        prev_stable = stable_map(prev, "4.0.8")
        prev_stable["channel-map"] = [e for e in prev_stable["channel-map"] if e["channel"]["architecture"] != "s390x"]
        edge_complete = edge_map_at(VERSION, revs)
        entries = edge_complete["channel-map"] + prev_stable["channel-map"]

        def fetch():
            return entries

        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_WAIT": "1", "NIFT_SNAP_POLL": "0", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            fetch,
        )
        self.assertEqual(code, 1)
        self.assertIn("invalid previous stable snapshot", out)
        self.assertNotIn("DRY-RUN: snapcraft release", out)
        self.assertNotIn("DRY-RUN: snapcraft promote", out)

    def test_malformed_previous_stable_prevents_candidate_mutation(self):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        prev_stable = stable_map(prev, "4.0.8")
        for entry in prev_stable["channel-map"]:
            if entry["channel"]["architecture"] == "amd64":
                del entry["revision"]
        entries = edge_map_at(VERSION, revs)["channel-map"] + prev_stable["channel-map"]

        def fetch():
            return entries

        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_WAIT": "1", "NIFT_SNAP_POLL": "0", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            fetch,
        )
        self.assertEqual(code, 1)
        self.assertIn("malformed", out)
        self.assertNotIn("DRY-RUN: snapcraft release", out)
        self.assertNotIn("DRY-RUN: snapcraft promote", out)

    def test_duplicate_previous_stable_prevents_candidate_mutation(self):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        prev_stable = stable_map(prev, "4.0.8")
        prev_stable["channel-map"] = prev_stable["channel-map"] + [
            channel_entry("amd64", 777, "4.0.8", risk="stable")]
        entries = edge_map_at(VERSION, revs)["channel-map"] + prev_stable["channel-map"]

        def fetch():
            return entries

        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_WAIT": "1", "NIFT_SNAP_POLL": "0", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            fetch,
        )
        self.assertEqual(code, 1)
        self.assertIn("duplicate", out)
        self.assertNotIn("DRY-RUN: snapcraft release", out)
        self.assertNotIn("DRY-RUN: snapcraft promote", out)

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
        prev = revision_numbers(offset=500)
        lagging = edge_map_at(VERSION, revs)
        for entry in lagging["channel-map"]:
            if entry["channel"]["architecture"] == "riscv64":
                entry["version"] = "4.0.8"
        entries = lagging["channel-map"] + stable_map(prev, "4.0.8")["channel-map"]

        def always_lagging():
            return entries

        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_WAIT": "1", "NIFT_SNAP_POLL": "0", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            always_lagging,
        )
        self.assertEqual(code, 1)
        self.assertIn("timed out", out)

    def test_malformed_edge_entry_fails_closed(self):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        entries = edge_map_at(VERSION, revs)["channel-map"] + stable_map(prev, "4.0.8")["channel-map"]
        for entry in entries:
            if entry["channel"]["architecture"] == "arm64" and entry["channel"]["risk"] == "edge":
                del entry["revision"]

        def malformed():
            return entries

        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_WAIT": "1", "NIFT_SNAP_POLL": "0", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            malformed,
        )
        self.assertEqual(code, 1)
        self.assertIn("malformed latest/edge entries", out)

    @staticmethod
    def stateful_fetch(states):
        index = {"n": 0}

        def fetch():
            state = states[min(index["n"], len(states) - 1)]
            index["n"] += 1
            return state

        return fetch

    def env(self, **extra):
        env = {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_WAIT": "1", "NIFT_SNAP_POLL": "0", "SNAPCRAFT_STORE_CREDENTIALS": "secret"}
        env.update(extra)
        return env

    def base_states(self, candidate_incomplete=False, stable_incomplete=False):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        prev_stable = stable_map(prev, "4.0.8")
        edge_waiting = edge_map_at(VERSION, revs)
        for entry in edge_waiting["channel-map"]:
            if entry["channel"]["architecture"] == "s390x":
                entry["version"] = "4.0.8"
        edge_complete = edge_map_at(VERSION, revs)
        candidate = candidate_map(revs)
        if candidate_incomplete:
            for entry in candidate["channel-map"]:
                if entry["channel"]["architecture"] == "s390x":
                    entry["version"] = "4.0.8"
        stable = stable_map(revs, VERSION)
        if stable_incomplete:
            for entry in stable["channel-map"]:
                if entry["channel"]["architecture"] == "s390x":
                    entry["version"] = "4.0.8"
        return prev_stable, edge_waiting, edge_complete, candidate, stable

    def test_delayed_candidate_converges(self):
        prev_stable, edge_waiting, edge_complete, candidate, stable = self.base_states()
        candidate_lag = dict(candidate)
        candidate_lag["channel-map"] = [e for e in candidate["channel-map"] if e["channel"]["architecture"] != "s390x"]

        def combine(*maps):
            entries = []
            for m in maps:
                entries.extend(m["channel-map"])
            return entries

        states = [
            combine(edge_waiting, prev_stable),
            combine(edge_waiting, prev_stable),
            combine(edge_complete, prev_stable),
            combine(candidate_lag, prev_stable),  # candidate not yet visible for s390x
            combine(candidate, prev_stable),      # candidate converges
            combine(candidate, stable),
        ]
        code, out = self.run_main(self.env(NIFT_SNAP_CANDIDATE_WAIT="2"), self.stateful_fetch(states))
        self.assertEqual(code, 0, out)
        self.assertIn("DRY-RUN: snapcraft release nift 100 latest/stable", out)
        self.assertNotIn("DRY-RUN: snapcraft promote", out)
        self.assertIn("Stable verified", out)

    def test_candidate_timeout_prevents_smoke_and_promotion(self):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        prev_stable = stable_map(prev, "4.0.8")
        edge_complete = edge_map_at(VERSION, revs)
        candidate = candidate_map(revs)
        for entry in candidate["channel-map"]:
            if entry["channel"]["architecture"] == "s390x":
                entry["version"] = "4.0.8"  # never converges
        entries = edge_complete["channel-map"] + prev_stable["channel-map"] + candidate["channel-map"]

        def steady():
            return entries

        code, out = self.run_main(self.env(NIFT_SNAP_CANDIDATE_WAIT="1"), steady)
        self.assertEqual(code, 1)
        self.assertIn("candidate did not converge", out)
        self.assertNotIn("DRY-RUN: bash packaging/snap-candidate-smoke.sh", out)
        self.assertNotIn("DRY-RUN: snapcraft promote", out)
        self.assertNotIn("latest/stable", out)

    def test_delayed_stable_converges(self):
        prev_stable, edge_waiting, edge_complete, candidate, stable = self.base_states()
        stable_lag = dict(stable)
        stable_lag["channel-map"] = [e for e in stable["channel-map"] if e["channel"]["architecture"] != "s390x"]

        def combine(*maps):
            entries = []
            for m in maps:
                entries.extend(m["channel-map"])
            return entries

        states = [
            combine(edge_waiting, prev_stable),
            combine(edge_waiting, prev_stable),
            combine(edge_complete, prev_stable),
            combine(candidate, prev_stable),
            combine(candidate, stable_lag),  # stable not yet visible for s390x
            combine(candidate, stable),      # stable converges
        ]
        code, out = self.run_main(self.env(NIFT_SNAP_STABLE_WAIT="2"), self.stateful_fetch(states))
        self.assertEqual(code, 0, out)
        self.assertIn("Stable verified", out)

    def test_stable_timeout_prints_rollback(self):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        prev_stable = stable_map(prev, "4.0.8")  # stable never advances past 4.0.8
        candidate = candidate_map(revs)
        entries = candidate["channel-map"] + prev_stable["channel-map"]

        def steady():
            return entries

        code, out = self.run_main(self.env(NIFT_SNAP_STABLE_WAIT="1"), steady)
        self.assertEqual(code, 1)
        self.assertIn("stable did not converge", out)
        self.assertIn("Rollback commands", out)
        self.assertIn("snapcraft release nift 600 latest/stable", out)  # previous amd64 revision
        self.assertNotIn("Stable verified", out)


class WorkflowStructure(unittest.TestCase):
    def load(self, path):
        with open(os.path.join(REPO, path), encoding="utf-8") as f:
            return f.read()

    def env_block_values(self, yaml_text):
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
        self.assertNotIn("$(date", text)
        for value in self.env_block_values(text):
            self.assertNotIn("$(", value)
            if "${" in value and "${{" not in value:
                self.fail("shell expansion in YAML env value: " + value)
        self.assertIn('"NIFT_SNAP_VERSION=${GITHUB_REF_NAME#v}" >> "$GITHUB_ENV"', text)

    def test_release_coordination_runs_the_coordinator(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertIn("release-coordination", text)
        self.assertIn("packaging/snap_release.py", text)

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

    def test_immutable_snapcraft_pin_with_post_install_assertions(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertIn('SNAPCRAFT_SNAP_REVISION: "18514"', text)
        self.assertIn('SNAPCRAFT_EXPECTED_VERSION: "9.0.1"', text)
        # Both the manual preflight and the release coordinator call the shared
        # pin script, which holds the immutable-revision install and assertions.
        self.assertEqual(text.count("packaging/snapcraft-pin.sh"), 2)
        script = self.load("packaging/snapcraft-pin.sh")
        self.assertIn('--revision="$REVISION"', script)
        self.assertIn('[ "$installed_version" = "$EXPECTED" ]', script)
        self.assertIn('[ "$installed_revision" = "$REVISION" ]', script)

    def test_manual_preflight_verifies_pin_without_credentials_or_publication(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertIn("toolchain-preflight", text)
        preflight = text.split("release-coordination")[0]
        self.assertIn("packaging/snapcraft-pin.sh", preflight)
        # No Store credential, no release/promote/upload publication in preflight.
        self.assertNotIn("SNAPCRAFT_STORE_CREDENTIALS", preflight)
        self.assertNotIn("snapcraft release", preflight)
        self.assertNotIn("snapcraft promote", preflight)
        self.assertNotIn("snapcraft upload", preflight)

    def test_release_coordination_depends_on_toolchain_preflight(self):
        self.assertIn("needs: [build, toolchain-preflight]", self.load(".github/workflows/snap.yml"))

    def test_candidate_smoke_ordered_before_stable_release(self):
        script = self.load("packaging/snap_release.py")
        smoke = script.index("snap-candidate-smoke.sh")
        release = script.index("stable_release_command(expected")
        self.assertLess(smoke, release)

    def test_no_whole_channel_promote(self):
        script = self.load("packaging/snap_release.py")
        self.assertNotIn("snapcraft\", \"promote\"", script)
        self.assertNotIn("promote_command", script)
        self.assertIn("snapcraft\", \"release\"", script)
        self.assertIn("latest/stable", script)

    def test_no_timestamp_window_selection(self):
        script = self.load("packaging/snap_release.py")
        self.assertNotIn("created_at", script)
        self.assertNotIn("NIFT_SNAP_START", script)
        self.assertNotIn("tolerance", script)

    def test_fetch_requests_the_fields_the_parser_needs(self):
        script = self.load("packaging/snap_release.py")
        self.assertIn("fields=channel-map,revision,version", script)
        self.assertNotIn("?fields=channel-map\"", script)

    def test_no_credential_skip_gate_on_publication(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertNotIn("SNAPCRAFT_STORE_CREDENTIALS != ''", text)

    def test_smoke_uses_sudo_for_install_and_cleanup(self):
        smoke = self.load("packaging/snap-candidate-smoke.sh")
        self.assertIn("sudo snap install nift", smoke)
        self.assertIn("sudo snap remove nift", smoke)

    def test_smoke_installs_exact_nift_revision(self):
        smoke = self.load("packaging/snap-candidate-smoke.sh")
        self.assertIn('sudo snap install nift --revision="$REVISION"', smoke)
        self.assertNotIn("--channel=latest/candidate", smoke)

    def test_snapcraft_version_assertion_parses_robustly(self):
        script = self.load("packaging/snapcraft-pin.sh")
        # `snapcraft version` commonly prints "snapcraft 9.0.1"; the version
        # token must be extracted, never compared as the raw whole-output string.
        self.assertIn("grep -oE '[0-9]+(\\.[0-9]+){1,2}'", script)
        self.assertNotIn('[ "$(snapcraft version)" = "$EXPECTED" ]', script)
        self.assertIn('[ "$installed_version" = "$EXPECTED" ]', script)
        self.assertIn('[ "$installed_revision" = "$REVISION" ]', script)

    def test_release_workflow_calls_snap_via_workflow_call(self):
        self.assertIn("uses: ./.github/workflows/snap.yml", self.load(".github/workflows/release.yml"))


if __name__ == "__main__":
    unittest.main(verbosity=2)