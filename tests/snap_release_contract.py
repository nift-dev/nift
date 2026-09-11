#!/usr/bin/env python3
"""Offline contract tests for the nift Snap publication coordinator.

Covers: the exact six-architecture set split into five required targets and one
best-effort target (riscv64); the manual mode-based coordinator (--status,
--check-ready, --promote-candidate, --promote-stable) with explicit
confirmation; no initial remote-build polling (a missing required build fails
fast on manual invocation); the real v2 channel-map schema (architecture inside
channel, released-at spelling, branch omitted when unbranched); the required
fields=channel-map,revision,version request; rejection of an API error-list and
of entries lacking revision/version; entry validation; polling only after a
deliberate channel mutation (candidate/stable convergence) with short bounded
timeouts; best-effort riscv64 never blocking or failing a required promotion;
no-downgrade refusal when stable riscv64 is already newer; legacy i386 ignored
and reported in edge/stable but fail-closed in candidate; exact edge revision
numbers carried through candidate and stable; a complete rollback snapshot
before any candidate mutation; candidate staging and confinement smoke ordered
before any stable mutation; the exact per-revision stable release invocation
(whole-channel promote never used); positional rollback syntax; idempotent
resumption of an interrupted or already-complete promotion; read-only status
performing no mutations; no live channel mutation or wait in the tag-triggered
release graph; no duplicate publisher targeting unbranched edge; literal YAML
shell expressions rejected; the actual immutable Snapcraft pin with post-install
assertions; sudo candidate install/cleanup; and missing publishing credentials
or explicit confirmation failing rather than skipping. No network access and no
Store operations are performed.
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

    def test_rollback_availability_splits_already_advanced_archs(self):
        expected = {arch: {"revision": rev, "version": "4.0.9"} for arch, rev in revision_numbers().items()}
        previous = {
            "amd64": {"revision": 100, "version": "4.0.9"},   # already advanced to target
            "arm64": {"revision": 101, "version": "4.0.9"},   # already advanced to target
            "armhf": {"revision": 602, "version": "4.0.8"},
            "ppc64el": {"revision": 603, "version": "4.0.8"},
            "riscv64": {"revision": 604, "version": "4.0.8"},
            "s390x": {"revision": 605, "version": "4.0.8"},
        }
        known, unknown = sr.rollback_availability(previous, expected)
        self.assertEqual(unknown, ["amd64", "arm64"])
        self.assertEqual(set(known), {"armhf", "ppc64el", "riscv64", "s390x"})
        for arch in unknown:
            self.assertNotIn(arch, known)

    def test_rollback_availability_full_pristine_snapshot(self):
        prev = revision_numbers(offset=500)
        expected = {arch: {"revision": rev, "version": "4.0.9"} for arch, rev in revision_numbers().items()}
        previous = {arch: {"revision": prev[arch], "version": "4.0.8"} for arch in ARCHS}
        known, unknown = sr.rollback_availability(previous, expected)
        self.assertEqual(unknown, [])
        self.assertEqual(set(known), set(ARCHS))

    def test_any_target_version_revision_is_not_recoverable(self):
        # v4.0.8 recovery: stable amd64 held revision 704 at 4.0.8 while the
        # screened target was revision 698 at 4.0.8. Revision 704 is another
        # v4.0.8 publication, not the original pre-v4.0.8 state, so it must be
        # classified unknown even though it differs from the selected revision.
        expected = {"amd64": {"revision": 698, "version": "4.0.8"}}
        previous = {"amd64": {"revision": 704, "version": "4.0.8"}}
        known, unknown = sr.rollback_availability(previous, expected)
        self.assertEqual(unknown, ["amd64"])
        self.assertEqual(known, {})
        self.assertNotIn("704", " ".join(sr.build_rollback_commands(known)))

    def test_older_version_is_known_pre_release_base(self):
        expected = {"amd64": {"revision": 698, "version": "4.0.8"}}
        previous = {"amd64": {"revision": 678, "version": "4.0.7"}}
        known, unknown = sr.rollback_availability(previous, expected)
        self.assertEqual(unknown, [])
        self.assertEqual(known["amd64"], {"revision": 678, "version": "4.0.7"})


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

    def build_states(self, candidate_extra=None, edge_lag_arch=None):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)  # previous stable revisions, 600-605
        prev_stable = stable_map(prev, "4.0.8")
        edge = edge_map_at(VERSION, revs)
        if edge_lag_arch:
            for entry in edge["channel-map"]:
                if entry["channel"]["architecture"] == edge_lag_arch:
                    entry["version"] = "4.0.8"
        candidate = candidate_map(revs)
        if candidate_extra:
            candidate["channel-map"].extend(candidate_extra)
        stable_new = stable_map(revs, VERSION)

        def combine(*maps):
            entries = []
            for m in maps:
                entries.extend(m["channel-map"])
            return entries

        # Two-phase mode flow. --promote-candidate fetches edge once (initial)
        # then polls candidate once; --promote-stable fetches candidate (initial),
        # candidate again (pre-release revalidation) and then stable (convergence).
        states = [
            combine(edge, prev_stable),
            combine(candidate, prev_stable),
            combine(candidate, prev_stable),
            combine(candidate, prev_stable),
            combine(candidate, stable_new),
        ]
        return self.stateful_fetch(states)

    def run_main(self, env, fetch, argv=("--status", "--dry-run")):
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

    def run_promotion(self, env, fetch):
        """Run the two manual phases (candidate then stable) in --dry-run."""
        env = dict(env)
        env["NIFT_SNAP_CONFIRM"] = "yes"
        code_c, out_c = self.run_main(env, fetch, ("--promote-candidate", "--dry-run"))
        code_s, out_s = self.run_main(env, fetch, ("--promote-stable", "--dry-run"))
        return code_c, out_c, code_s, out_s

    def test_successful_coordination_via_edge(self):
        code_c, out_c, code_s, out_s = self.run_promotion(self.env(), self.build_states())
        self.assertEqual(code_c, 0, out_c)
        self.assertEqual(code_s, 0, out_s)
        self.assertIn("DRY-RUN: snapcraft release nift 100 latest/candidate", out_c)
        self.assertIn("DRY-RUN: bash packaging/snap-candidate-smoke.sh", out_s)
        self.assertNotIn("DRY-RUN: snapcraft promote", out_c + out_s)
        revs = revision_numbers()
        for arch in ARCHS:
            self.assertIn("DRY-RUN: snapcraft release nift {} latest/stable".format(revs[arch]), out_s)
        self.assertIn("Stable verified", out_s)

    def test_legacy_candidate_never_promoted(self):
        extra = [channel_entry("i386", 11, "3.0.3", risk="candidate")]
        code_c, out_c, code_s, out_s = self.run_promotion(self.env(), self.build_states(candidate_extra=extra))
        self.assertEqual(code_c, 1, out_c)
        self.assertIn("unsupported candidate entry i386", out_c)
        self.assertNotIn("DRY-RUN: snapcraft promote", out_c)
        self.assertNotIn("latest/stable", out_c)

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
            combine(candidate, prev_stable),
            combine(candidate, prev_stable),
            combine(candidate, prev_stable),
            combine(candidate, stable_new),
        ]
        code_c, out_c, code_s, out_s = self.run_promotion(self.env(), self.stateful_fetch(states))
        self.assertEqual(code_c, 0, out_c)
        self.assertEqual(code_s, 0, out_s)
        self.assertNotIn("DRY-RUN: snapcraft promote", out_c + out_s)
        for arch in ARCHS:
            self.assertIn("DRY-RUN: snapcraft release nift {} latest/stable".format(revs[arch]), out_s)
        self.assertNotIn("DRY-RUN: snapcraft release nift 11 latest/stable", out_s)
        self.assertIn("i386", out_c + out_s)  # reported as legacy, never released
        self.assertIn("Stable verified", out_s)

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
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_CONFIRM": "yes", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            fetch, ("--promote-candidate", "--dry-run"),
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
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_CONFIRM": "yes", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            fetch, ("--promote-candidate", "--dry-run"),
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
            {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_CONFIRM": "yes", "SNAPCRAFT_STORE_CREDENTIALS": "secret"},
            fetch, ("--promote-candidate", "--dry-run"),
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
            os.environ.update({"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_CONFIRM": "yes"})
            self.assertEqual(sr.main(["--promote-candidate"]), 2)
        finally:
            sr.fetch_channel_map = saved_fetch
            os.environ.clear()
            os.environ.update(saved_env)

    def test_missing_confirmation_fails_closed_before_network(self):
        def boom():
            raise AssertionError("fetch must not be called without explicit confirmation")
        saved_fetch = sr.fetch_channel_map
        saved_env = dict(os.environ)
        try:
            sr.fetch_channel_map = boom
            os.environ.update({"NIFT_SNAP_VERSION": VERSION, "SNAPCRAFT_STORE_CREDENTIALS": "secret"})
            self.assertEqual(sr.main(["--promote-candidate"]), 2)
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

    def test_required_missing_fails_fast_no_wait(self):
        # A missing required build must fail immediately on manual invocation:
        # there is no initial two-hour polling design anymore.
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        lagging = edge_map_at(VERSION, revs)
        for entry in lagging["channel-map"]:
            if entry["channel"]["architecture"] == "s390x":
                entry["version"] = "4.0.8"
        entries = lagging["channel-map"] + stable_map(prev, "4.0.8")["channel-map"]

        def always_lagging():
            return entries

        code, out = self.run_main(self.env(), always_lagging, ("--promote-candidate", "--dry-run"))
        self.assertEqual(code, 1)
        self.assertIn("required architecture(s) missing", out)
        self.assertIn("s390x", out)
        self.assertNotIn("timed out", out)
        self.assertNotIn("DRY-RUN: snapcraft release", out)

    def test_best_effort_riscv64_lagging_does_not_block(self):
        # riscv64 is best-effort: a lagging riscv64 edge build must not delay or
        # fail promotion of the five required architectures.
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        prev_stable = stable_map(prev, "4.0.8")
        edge = edge_map_at(VERSION, revs)
        for entry in edge["channel-map"]:
            if entry["channel"]["architecture"] == "riscv64":
                entry["version"] = "4.0.8"
        candidate = candidate_map(revs)
        candidate["channel-map"] = [e for e in candidate["channel-map"] if e["channel"]["architecture"] != "riscv64"]
        stable_new = stable_map(revs, VERSION)
        stable_new["channel-map"] = [e for e in stable_new["channel-map"] if e["channel"]["architecture"] != "riscv64"]

        def combine(*maps):
            entries = []
            for m in maps:
                entries.extend(m["channel-map"])
            return entries

        states = [
            combine(edge, prev_stable),
            combine(candidate, prev_stable),
            combine(candidate, prev_stable),
            combine(candidate, prev_stable),
            combine(candidate, stable_new),
        ]
        code_c, out_c, code_s, out_s = self.run_promotion(self.env(), self.stateful_fetch(states))
        self.assertEqual(code_c, 0, out_c)
        self.assertEqual(code_s, 0, out_s)
        self.assertIn("SKIP best-effort riscv64", out_c)
        self.assertIn("Best-effort not promoted", out_s)
        for arch in sorted(sr.REQUIRED_ARCHS):
            self.assertIn("DRY-RUN: snapcraft release nift {} latest/stable".format(revs[arch]), out_s)
        self.assertNotIn("DRY-RUN: snapcraft release nift {} latest/stable".format(revs["riscv64"]), out_s)
        self.assertIn("Stable verified", out_s)

    def test_status_is_read_only(self):
        # --status performs no channel mutations and needs no credentials.
        entries = edge_map_at(VERSION, revision_numbers())["channel-map"]
        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION},
            lambda: entries, ("--status",),
        )
        self.assertEqual(code, 0, out)
        self.assertIn("Snap state", out)
        self.assertIn("riscv64", out)
        self.assertNotIn("DRY-RUN: snapcraft release", out)

    def test_check_ready_required_all_ready(self):
        entries = edge_map_at(VERSION, revision_numbers())["channel-map"]
        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION},
            lambda: entries, ("--check-ready",),
        )
        self.assertEqual(code, 0, out)
        self.assertIn("READY", out)

    def test_check_ready_missing_required_fails(self):
        entries = edge_map_at(VERSION, revision_numbers())["channel-map"]
        entries = [e for e in entries if e["channel"]["architecture"] != "ppc64el"]
        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION},
            lambda: entries, ("--check-ready",),
        )
        self.assertEqual(code, 1, out)
        self.assertIn("ppc64el", out)

    def test_best_effort_older_on_stable_reported_honestly(self):
        # riscv64 may remain on an older stable version; status reports it.
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        edge = edge_map_at(VERSION, revs)
        candidate = candidate_map(revs)
        candidate["channel-map"] = [e for e in candidate["channel-map"] if e["channel"]["architecture"] != "riscv64"]
        stable_old = stable_map(prev, "4.0.9")
        entries = edge["channel-map"] + candidate["channel-map"] + stable_old["channel-map"]
        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION},
            lambda: entries, ("--status",),
        )
        self.assertEqual(code, 0, out)
        self.assertIn("best-effort", out)
        self.assertIn("riscv64", out)

    def test_best_effort_older_delayed_build_refuses_downgrade_of_newer_stable(self):
        # riscv64 is staged on candidate at the target version, but latest/stable
        # riscv64 is already at a NEWER version (promoted separately earlier):
        # releasing the older target revision would downgrade stable, so the
        # best-effort promotion is refused while the required set publishes.
        target = "4.0.11"
        new = "4.0.12"
        revs = {"amd64": 758, "arm64": 760, "armhf": 762, "ppc64el": 761, "s390x": 759}
        rv = {"riscv64": 764}
        candidate = {"channel-map": [channel_entry(arch, rev, target, risk="candidate") for arch, rev in revs.items()] +
                                     [channel_entry("riscv64", rv["riscv64"], target, risk="candidate")]}
        stable_new = {"channel-map": [channel_entry(arch, rev, target, risk="stable") for arch, rev in revs.items()] +
                                      [channel_entry("riscv64", 746, new, risk="stable")]}
        prev_stable = {"channel-map": [channel_entry(arch, rev - 600, "4.0.10", risk="stable") for arch, rev in revs.items()]}

        def combine(*maps):
            entries = []
            for m in maps:
                entries.extend(m["channel-map"])
            return entries

        states = [
            combine(candidate, stable_new),
            combine(candidate, stable_new),
            combine(candidate, stable_new),
            combine(candidate, stable_new),
        ]
        code, out = self.run_main(
            self.env(NIFT_SNAP_VERSION=target), self.stateful_fetch(states),
            ("--promote-stable", "--dry-run"),
        )
        self.assertEqual(code, 0, out)
        self.assertIn("refusing to downgrade", out)
        self.assertNotIn("DRY-RUN: snapcraft release nift {} latest/stable".format(rv["riscv64"]), out)
        for arch in sorted(revs):
            self.assertIn("DRY-RUN: snapcraft release nift {} latest/stable".format(revs[arch]), out)
        self.assertIn("Stable verified", out)

    def test_promotion_resumes_idempotently(self):
        # Interrupted promotion resumes safely: an already-staged candidate and
        # an already-correct stable assignment are preserved on rerun, and a
        # candidate already at the version is not re-staged.
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        prev_stable = stable_map(prev, "4.0.8")
        edge = edge_map_at(VERSION, revs)
        candidate = candidate_map(revs)
        stable = stable_map(revs, VERSION)
        # candidate already staged and amd64/arm64 already on stable (partial
        # prior publication). This rerun must preserve those and complete the rest.
        partial_stable = dict(stable)
        partial_stable["channel-map"] = [
            channel_entry("amd64", revs["amd64"], VERSION, risk="stable"),
            channel_entry("arm64", revs["arm64"], VERSION, risk="stable"),
        ] + [e for e in partial_stable["channel-map"]
             if e["channel"]["architecture"] in ("armhf", "ppc64el", "riscv64", "s390x")]

        def combine(*maps):
            entries = []
            for m in maps:
                entries.extend(m["channel-map"])
            return entries

        states = [
            combine(edge, candidate, partial_stable),
            combine(candidate, partial_stable),
            combine(candidate, partial_stable),
            combine(candidate, partial_stable),
            combine(candidate, stable),
        ]
        code_c, out_c, code_s, out_s = self.run_promotion(self.env(), self.stateful_fetch(states))
        self.assertEqual(code_c, 0, out_c)
        self.assertEqual(code_s, 0, out_s)
        # Candidate was already at the version: no re-staging of candidate.
        self.assertNotIn("DRY-RUN: snapcraft release nift 100 latest/candidate", out_c)
        # amd64/arm64 already at their selected stable revisions are not
        # re-released (idempotent per-revision assignments are preserved).
        self.assertIn("DRY-RUN: snapcraft release nift {} latest/stable".format(revs["armhf"]), out_s)
        self.assertIn("DRY-RUN: snapcraft release nift {} latest/stable".format(revs["s390x"]), out_s)
        self.assertIn("Stable verified", out_s)

    def test_best_effort_queued_reported_pending(self):
        # riscv64 with no edge build yet is reported as pending, never blocking.
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        edge = edge_map_at(VERSION, revs)
        edge["channel-map"] = [e for e in edge["channel-map"] if e["channel"]["architecture"] != "riscv64"]
        entries = edge["channel-map"] + stable_map(prev, "4.0.9")["channel-map"]
        code, out = self.run_main(
            {"NIFT_SNAP_VERSION": VERSION},
            lambda: entries, ("--check-ready",),
        )
        self.assertEqual(code, 0, out)
        self.assertIn("READY", out)
        self.assertIn("riscv64", out)
        self.assertIn("pending", out)

    def test_malformed_edge_entry_fails_closed(self):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        entries = edge_map_at(VERSION, revs)["channel-map"] + stable_map(prev, "4.0.8")["channel-map"]
        for entry in entries:
            if entry["channel"]["architecture"] == "arm64" and entry["channel"]["risk"] == "edge":
                del entry["revision"]

        def malformed():
            return entries

        code, out = self.run_main(self.env(), malformed, ("--promote-candidate", "--dry-run"))
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
        env = {"NIFT_SNAP_VERSION": VERSION, "NIFT_SNAP_POLL": "0",
               "NIFT_SNAP_CANDIDATE_WAIT": "1", "NIFT_SNAP_STABLE_WAIT": "1",
               "NIFT_SNAP_CONFIRM": "yes", "SNAPCRAFT_STORE_CREDENTIALS": "secret"}
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
            combine(edge_complete, prev_stable),   # candidate phase initial
            combine(candidate_lag, prev_stable),   # candidate not yet visible for s390x
            combine(candidate, prev_stable),       # candidate converges
            combine(candidate, prev_stable),       # stable phase initial
            combine(candidate, prev_stable),       # stable phase revalidate
            combine(candidate, stable),            # stable converges
        ]
        code_c, out_c, code_s, out_s = self.run_promotion(
            self.env(NIFT_SNAP_CANDIDATE_WAIT="2"), self.stateful_fetch(states))
        self.assertEqual(code_c, 0, out_c)
        self.assertEqual(code_s, 0, out_s)
        self.assertIn("DRY-RUN: snapcraft release nift 100 latest/stable", out_s)
        self.assertNotIn("DRY-RUN: snapcraft promote", out_c + out_s)
        self.assertIn("Stable verified", out_s)

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

        code, out = self.run_main(self.env(NIFT_SNAP_CANDIDATE_WAIT="1"), steady,
                                  ("--promote-candidate", "--dry-run"))
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
            combine(candidate, prev_stable),   # stable phase initial
            combine(candidate, prev_stable),   # revalidation
            combine(candidate, stable_lag),    # stable not yet visible for s390x
            combine(candidate, stable),        # stable converges
        ]
        code, out = self.run_main(self.env(NIFT_SNAP_STABLE_WAIT="2"), self.stateful_fetch(states),
                                  ("--promote-stable", "--dry-run"))
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

        code, out = self.run_main(self.env(NIFT_SNAP_STABLE_WAIT="1"), steady,
                                  ("--promote-stable", "--dry-run"))
        self.assertEqual(code, 1)
        self.assertIn("stable did not converge", out)
        self.assertIn("Rollback commands", out)
        self.assertIn("snapcraft release nift 600 latest/stable", out)  # previous amd64 revision
        self.assertNotIn("Stable verified", out)

    def test_partial_rerun_rollback_is_not_authoritative(self):
        revs = revision_numbers()
        prev = revision_numbers(offset=500)
        # Stable already partially advanced by a prior publication: amd64 and
        # arm64 are at the target revisions; the other four are pre-release.
        partial = dict(stable_map(prev, "4.0.8"))
        partial["channel-map"] = [
            channel_entry("amd64", revs["amd64"], VERSION, risk="stable"),
            channel_entry("arm64", revs["arm64"], VERSION, risk="stable"),
        ] + [e for e in partial["channel-map"]
             if e["channel"]["architecture"] in ("armhf", "ppc64el", "riscv64", "s390x")]
        candidate = candidate_map(revs)
        entries = candidate["channel-map"] + partial["channel-map"]

        def steady():
            return entries

        code, out = self.run_main(self.env(NIFT_SNAP_STABLE_WAIT="1"), steady,
                                  ("--promote-stable", "--dry-run"))
        self.assertEqual(code, 1)
        self.assertIn("stable did not converge", out)
        self.assertIn("not recoverable from the channel map for: amd64, arm64", out)
        # The rollback section is explicitly partial and never labels itself
        # an authoritative complete rollback plan.
        self.assertNotIn("previous stable, per-arch", out)
        self.assertNotIn("(manual, per-arch, non-atomic)", out)
        self.assertIn("Rollback commands (per-arch; only architectures whose original pre-release", out)
        rollback_section = out[out.index("Rollback commands"):]
        self.assertIn("snapcraft release nift 602 latest/stable", rollback_section)
        self.assertIn("snapcraft release nift 605 latest/stable", rollback_section)
        self.assertNotIn("snapcraft release nift 100 latest/stable", rollback_section)
        self.assertNotIn("snapcraft release nift 101 latest/stable", rollback_section)

    def test_target_version_any_revision_resumes_toward_screened_revision(self):
        # The v4.0.8 recovery case: stable amd64 already exposes the target
        # version at revision 704 while the screened target is revision 698.
        # 704 is another v4.0.8 publication, so it must be classified
        # non-recoverable, never printed as a rollback command, and the run
        # must still resume publication toward 698.
        target = "4.0.8"
        revs = {"amd64": 698, "arm64": 702, "armhf": 699, "ppc64el": 700, "riscv64": 687, "s390x": 701}
        candidate = {"channel-map": [channel_entry(arch, rev, target, risk="candidate") for arch, rev in revs.items()]}
        partial = [
            channel_entry("amd64", 704, target, risk="stable"),      # wrong target-version revision
            channel_entry("arm64", 702, target, risk="stable"),      # exact screened revision
            channel_entry("armhf", 675, "4.0.7", risk="stable"),
            channel_entry("ppc64el", 679, "4.0.7", risk="stable"),
            channel_entry("riscv64", 594, "4.0.7", risk="stable"),
            channel_entry("s390x", 668, "4.0.7", risk="stable"),     # never advances
        ]
        entries = candidate["channel-map"] + partial

        def steady():
            return entries

        code, out = self.run_main(self.env(NIFT_SNAP_VERSION="4.0.8", NIFT_SNAP_STABLE_WAIT="1"), steady,
                                  ("--promote-stable", "--dry-run"))
        self.assertEqual(code, 1)
        self.assertIn("not recoverable from the channel map for: amd64, arm64", out)
        # The diagnostic must not describe every target-version assignment as
        # already correct: amd64@704 is the wrong revision of the target version.
        self.assertNotIn("already-correct", out)
        # It must say publication resumes toward the exact selected revisions...
        self.assertIn("resumes publication toward the exact selected revisions", out)
        # ...and that a different target-version revision (704 vs selected 698)
        # may be corrected while the exact match (arm64@702) stays unchanged.
        self.assertIn("Assignments already at their selected revision remain unchanged", out)
        self.assertIn("other revisions of the target version may be corrected", out)
        self.assertIn("DRY-RUN: snapcraft release nift 698 latest/stable", out)  # resume toward screened revision
        rollback_section = out[out.index("Rollback commands"):]
        self.assertIn("snapcraft release nift 675 latest/stable", rollback_section)
        self.assertIn("snapcraft release nift 668 latest/stable", rollback_section)
        self.assertNotIn("704 latest/stable", rollback_section)
        self.assertNotIn("698 latest/stable", rollback_section)
        self.assertNotIn("previous stable, per-arch", out)
        self.assertNotIn("(manual, per-arch, non-atomic)", out)


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
        self.assertIn('test "$package_version" = "${GITHUB_REF_NAME#v}"', text)

    def test_release_validation_does_not_coordinate_store_promotion(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertNotIn("release-coordination", text)
        self.assertNotIn("packaging/snap_release.py", text)
        self.assertNotIn("SNAPCRAFT_STORE_CREDENTIALS", text)

    def test_manual_promotion_runs_the_coordinator(self):
        text = self.load(".github/workflows/snap-promote.yml")
        self.assertIn("workflow_dispatch", text)
        self.assertIn("packaging/snap_release.py", text)
        self.assertIn("nift-snap-store-promotion", text)
        self.assertIn("cancel-in-progress: false", text)
        self.assertEqual(text.count("SNAPCRAFT_STORE_CREDENTIALS:"), 1)
        self.assertNotIn("push:", text)

    def test_no_duplicate_publisher_to_unbranched_edge(self):
        for path in (".github/workflows/snap.yml", ".github/workflows/snap-promote.yml"):
            text = self.load(path)
            self.assertNotIn("publish-edge", text)
            self.assertNotIn("release=edge", text)
            self.assertNotIn("--release=edge", text)

    def test_immutable_snapcraft_pin_with_post_install_assertions(self):
        validation = self.load(".github/workflows/snap.yml")
        promotion = self.load(".github/workflows/snap-promote.yml")
        for text in (validation, promotion):
            self.assertIn('SNAPCRAFT_SNAP_REVISION: "18514"', text)
            self.assertIn('SNAPCRAFT_EXPECTED_VERSION: "9.0.1"', text)
            self.assertIn("packaging/snapcraft-pin.sh", text)
        script = self.load("packaging/snapcraft-pin.sh")
        self.assertIn('--revision="$REVISION"', script)
        self.assertIn('[ "$installed_version" = "$EXPECTED" ]', script)
        self.assertIn('[ "$installed_revision" = "$REVISION" ]', script)

    def test_manual_preflight_verifies_pin_without_credentials_or_publication(self):
        text = self.load(".github/workflows/snap.yml")
        self.assertIn("toolchain-preflight", text)
        self.assertIn("packaging/snapcraft-pin.sh", text)
        self.assertNotIn("SNAPCRAFT_STORE_CREDENTIALS", text)
        self.assertNotIn("snapcraft release", text)
        self.assertNotIn("snapcraft promote", text)
        self.assertNotIn("snapcraft upload", text)

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

    def test_no_stale_whole_channel_promotion_wording_in_active_docs(self):
        stale_phrases = [
            "promotes candidate -> stable",
            "before the promote step",
            "candidate set is promoted",
            "stable promotion",
            "release/promote",
            "before promotion",
            "promotion is refused",
        ]
        active_docs = (
            ".github/workflows/snap.yml",
            ".github/workflows/snap-promote.yml",
            "docs/handover/PACKAGING.md",
            "packaging/snap-candidate-smoke.sh",
            "packaging/snap_release.py",
        )
        for path in active_docs:
            text = self.load(path)
            for phrase in stale_phrases:
                self.assertNotIn(phrase, text,
                                 "stale whole-channel promotion wording '{}' in {}".format(phrase, path))

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
        text = self.load(".github/workflows/snap-promote.yml")
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

    def test_release_workflow_does_not_invoke_snap_packaging(self):
        # Phase 2 (GitHub release) must end before any package-manager work.
        # release.yml must not call snap/chocolatey/homebrew, and no
        # reusable-workflow call, workflow_run or dependency job may start
        # packaging after a successful release.
        text = self.load(".github/workflows/release.yml")
        self.assertNotIn("uses: ./.github/workflows/snap.yml", text)
        self.assertNotIn("uses: ./.github/workflows/chocolatey.yml", text)
        self.assertNotIn("uses: ./.github/workflows/homebrew.yml", text)
        # No actual workflow_run trigger (comments may mention the term).
        self.assertNotIn("workflow_run:", text)
        self.assertNotIn("packaging/snap_release.py", text)
        self.assertNotIn("SNAPCRAFT_STORE_CREDENTIALS", text)

    def test_snap_packaging_is_manual_only(self):
        # snap.yml must be separately manually invoked (workflow_dispatch) and
        # must not be reachable automatically from release.yml.
        snap_text = self.load(".github/workflows/snap.yml")
        self.assertIn("workflow_dispatch", snap_text)
        release_text = self.load(".github/workflows/release.yml")
        self.assertNotIn("uses: ./.github/workflows/snap.yml", release_text)
        self.assertNotIn("snap:", release_text.split("jobs:")[1] if "jobs:" in release_text else release_text)

    def test_tag_release_workflow_has_no_snap_wait_or_store_mutation(self):
        # The tag-triggered release graph must not wait on remote builders and
        # must not contain any live Snap Store channel mutation or packaging
        # invocation.
        for path in (".github/workflows/release.yml", ".github/workflows/snap.yml"):
            text = self.load(path)
            self.assertNotIn("NIFT_SNAP_WAIT", text)
            self.assertNotIn("release-coordination", text)
            self.assertNotIn("packaging/snap_release.py", text)
            self.assertNotIn("SNAPCRAFT_STORE_CREDENTIALS", text)
        text = self.load(".github/workflows/release.yml")
        self.assertNotIn("uses: ./.github/workflows/snap.yml", text)
