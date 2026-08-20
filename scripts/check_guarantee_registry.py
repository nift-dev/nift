#!/usr/bin/env python3
"""Structural validator for docs/guarantees/registry.json.

BH1 intentionally keeps this checker dumb and auditable. It verifies references,
allowed states, declared CI jobs, claim-source needles when sibling repositories
are available, and internal cross-links. It does not try to decide whether prose
semantically overstates evidence; that remains a human/agent audit backed by the
registry.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

ALLOWED_STATES = {"ESTABLISHED", "NOT_ESTABLISHED", "ACCEPTED_LIMITATION", "OUT_OF_SCOPE"}
ALLOWED_EVIDENCE = {"RETAINED", "CI_GATED", "CROSS_PLATFORM_GATED", "RELEASE_GATED", "CAMPAIGN", "FIELD"}
ALLOWED_TOT = {"VERIFIED_GUARD", "UNPROVEN", "PENDING_REVIEWER", "NOT_APPLICABLE"}
ALLOWED_ENFORCEMENT = {"MANUAL", "CI_GATED", "CROSS_PLATFORM_GATED", "RELEASE_GATED", "SCHEDULED"}
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*$")


class CheckFailure(Exception):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise CheckFailure(f"registry-not-found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise CheckFailure(f"registry-json-invalid: {path}:{exc.lineno}:{exc.colno}: {exc.msg}") from exc
    if not isinstance(data, dict):
        raise CheckFailure("registry-root-invalid: expected JSON object")
    return data


def repo_roots(registry_path: Path, data: dict[str, Any], args: argparse.Namespace) -> dict[str, Path | None]:
    nift_root = Path(args.nift_root).resolve() if args.nift_root else registry_path.resolve().parents[2]
    roots: dict[str, Path | None] = {"nift": nift_root}
    configured = data.get("repositories", {})
    if not isinstance(configured, dict):
        raise CheckFailure("repositories-invalid: expected object")
    explicit = {"website": args.website_root, "regression": args.regression_root}
    for name in ("website", "regression"):
        if explicit[name]:
            roots[name] = Path(explicit[name]).resolve()
            continue
        rel = configured.get(name)
        if isinstance(rel, str):
            candidate = (nift_root / rel).resolve()
            roots[name] = candidate if candidate.exists() else None
        else:
            roots[name] = None
    return roots


def path_for(ref: dict[str, Any], roots: dict[str, Path | None]) -> tuple[Path | None, str]:
    repo = ref.get("repo")
    path = ref.get("path")
    if repo not in roots:
        raise CheckFailure(f"reference-repo-invalid: {repo!r}")
    if not isinstance(path, str) or not path or Path(path).is_absolute() or ".." in Path(path).parts:
        raise CheckFailure(f"reference-path-invalid: repo={repo!r} path={path!r}")
    root = roots[repo]
    return ((root / path) if root is not None else None, repo)


def validate_ref(ref: Any, roots: dict[str, Path | None], label: str, require_needle: bool = False) -> bool:
    if not isinstance(ref, dict):
        raise CheckFailure(f"{label}-invalid: expected object")
    path, repo = path_for(ref, roots)
    needle = ref.get("needle")
    if require_needle and (not isinstance(needle, str) or not needle):
        raise CheckFailure(f"{label}-needle-missing")
    if needle is not None and not isinstance(needle, str):
        raise CheckFailure(f"{label}-needle-invalid")
    if path is None:
        return False
    if not path.is_file():
        raise CheckFailure(f"{label}-missing: {repo}:{ref['path']}")
    if needle is not None:
        text = path.read_text(encoding="utf-8", errors="replace")
        if needle not in text:
            raise CheckFailure(f"{label}-needle-not-found: {repo}:{ref['path']}: {needle!r}")
    return True



def validate_guard_ref(ref: Any, roots: dict[str, Path | None], label: str) -> bool:
    """Require guard refs to name executable/test artifacts, not passive prose."""
    checked = validate_ref(ref, roots, label)
    repo = ref.get("repo") if isinstance(ref, dict) else None
    path = ref.get("path") if isinstance(ref, dict) else None
    if repo not in {"nift", "regression"}:
        raise CheckFailure(f"{label}-repo-not-executable: {repo!r}")
    if not isinstance(path, str):
        raise CheckFailure(f"{label}-path-invalid")
    name = Path(path).name
    suffix = Path(path).suffix
    if name == "Makefile":
        needle = ref.get("needle")
        if not isinstance(needle, str) or not needle.strip():
            raise CheckFailure(f"{label}-make-target-needle-missing")
    elif suffix not in {".py", ".sh", ".cpp", ".cc", ".cxx", ".yml", ".yaml"}:
        raise CheckFailure(f"{label}-not-executable-artifact: {repo}:{path}")
    return checked


def make_target_from_command(command: str) -> tuple[Path, str] | None:
    """Extract the Makefile and target from the simple make commands used here."""
    parts = command.split()
    if not parts or parts[0] != "make":
        return None
    directory = Path(".")
    i = 1
    if i < len(parts) and parts[i] == "-C":
        if i + 1 >= len(parts):
            return None
        directory = Path(parts[i + 1])
        i += 2
    while i < len(parts) and (parts[i].startswith("-") or "=" in parts[i]):
        i += 1
    if i >= len(parts):
        return None
    return directory / "Makefile", parts[i]


def makefile_has_target(path: Path, target: str) -> bool:
    if not path.is_file():
        return False
    pattern = re.compile(rf"^{re.escape(target)}\s*:(?:\s|$)", re.MULTILINE)
    return bool(pattern.search(path.read_text(encoding="utf-8", errors="replace")))

def workflow_triggers(path: Path) -> set[str]:
    """Return top-level workflow triggers from the ordinary block-style YAML used here."""
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    triggers: set[str] = set()
    in_on = False
    for line in lines:
        if re.match(r"^on:\s*(?:#.*)?$", line):
            in_on = True
            continue
        if not in_on:
            continue
        if line and not line[0].isspace():
            break
        match = re.match(r"^  ([A-Za-z_][A-Za-z0-9_-]*):(?:\s|$)", line)
        if match:
            triggers.add(match.group(1))
    return triggers


def validate_workflow_trigger(path: Path, klass: str, gid: str) -> None:
    triggers = workflow_triggers(path)
    if klass in {"CI_GATED", "CROSS_PLATFORM_GATED"}:
        if not triggers.intersection({"push", "pull_request"}):
            raise CheckFailure(f"workflow-trigger-not-automatic: {gid}: {klass}: {path}")
    elif klass == "RELEASE_GATED":
        if not triggers.intersection({"workflow_dispatch", "release", "push"}):
            raise CheckFailure(f"workflow-trigger-not-release-capable: {gid}: {path}")
    elif klass == "SCHEDULED" and "schedule" not in triggers:
        raise CheckFailure(f"workflow-trigger-not-scheduled: {gid}: {path}")


def workflow_has_job(path: Path, job: str) -> bool:
    # Workflows in this repository use ordinary two-space job keys. Avoid a YAML
    # dependency for a deliberately tiny structural checker.
    pattern = re.compile(rf"^  {re.escape(job)}:\s*(?:#.*)?$", re.MULTILINE)
    return bool(pattern.search(path.read_text(encoding="utf-8", errors="replace")))


def validate_id(value: Any, label: str, seen: set[str]) -> str:
    if not isinstance(value, str) or not ID_RE.fullmatch(value):
        raise CheckFailure(f"{label}-id-invalid: {value!r}")
    if value in seen:
        raise CheckFailure(f"duplicate-id: {value}")
    seen.add(value)
    return value


def check(registry_path: Path, args: argparse.Namespace) -> dict[str, Any]:
    data = load_json(registry_path)
    if data.get("schema_version") != 1:
        raise CheckFailure(f"schema-version-unsupported: {data.get('schema_version')!r}")
    roots = repo_roots(registry_path, data, args)

    declared_states = data.get("states")
    declared_evidence = data.get("evidence_classes")
    declared_tot = data.get("test_of_test_states")
    if set(declared_states or []) != ALLOWED_STATES:
        raise CheckFailure("states-declaration-mismatch")
    if set(declared_evidence or []) != ALLOWED_EVIDENCE:
        raise CheckFailure("evidence-classes-declaration-mismatch")
    if set(declared_tot or []) != ALLOWED_TOT:
        raise CheckFailure("test-of-test-states-declaration-mismatch")

    baseline = data.get("baseline")
    if not isinstance(baseline, dict):
        raise CheckFailure("baseline-invalid: expected object")
    dev_version = baseline.get("development_version")
    if not isinstance(dev_version, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", dev_version):
        raise CheckFailure(f"baseline-development-version-invalid: {dev_version!r}")
    nift_root = roots["nift"]
    assert nift_root is not None
    cli = nift_root / "src/CLI.cpp"
    if not cli.is_file() or f"Nift v{dev_version}" not in cli.read_text(encoding="utf-8", errors="replace"):
        raise CheckFailure(f"baseline-development-version-stale: Nift v{dev_version} not found in src/CLI.cpp")

    guarantees = data.get("guarantees")
    claims = data.get("public_claims")
    claim_surfaces = data.get("public_claim_surfaces")
    discrepancies = data.get("known_discrepancies")
    meta_guards = data.get("meta_guards")
    if not isinstance(guarantees, list) or not guarantees:
        raise CheckFailure("guarantees-invalid: expected non-empty list")
    if not isinstance(claims, list):
        raise CheckFailure("public-claims-invalid: expected list")
    if not isinstance(claim_surfaces, list) or not claim_surfaces:
        raise CheckFailure("public-claim-surfaces-invalid: expected non-empty list")
    if not isinstance(discrepancies, list):
        raise CheckFailure("known-discrepancies-invalid: expected list")
    if not isinstance(meta_guards, list):
        raise CheckFailure("meta-guards-invalid: expected list")

    pinned_surface_keys = {
        (surface.get("repo"), surface.get("path"))
        for surface in claim_surfaces if isinstance(surface, dict)
    }

    guarantee_ids: set[str] = set()
    external_skips: set[str] = set()
    ci_refs = 0
    local_refs = 0

    for g in guarantees:
        if not isinstance(g, dict):
            raise CheckFailure("guarantee-invalid: expected object")
        gid = validate_id(g.get("id"), "guarantee", guarantee_ids)
        if g.get("state") not in ALLOWED_STATES:
            raise CheckFailure(f"guarantee-state-invalid: {gid}: {g.get('state')!r}")
        if not isinstance(g.get("guarantee"), str) or not g["guarantee"].strip():
            raise CheckFailure(f"guarantee-text-missing: {gid}")
        if not isinstance(g.get("scope"), str) or not g["scope"].strip():
            raise CheckFailure(f"guarantee-scope-missing: {gid}")
        evidence_classes = g.get("evidence_classes")
        if not isinstance(evidence_classes, list) or any(x not in ALLOWED_EVIDENCE for x in evidence_classes):
            raise CheckFailure(f"evidence-class-invalid: {gid}: {evidence_classes!r}")
        if g.get("test_of_test") not in ALLOWED_TOT:
            raise CheckFailure(f"test-of-test-invalid: {gid}: {g.get('test_of_test')!r}")
        evidence_refs = g.get("evidence_refs")
        guard_refs = g.get("guard_refs")
        if not isinstance(evidence_refs, list):
            raise CheckFailure(f"evidence_refs-invalid: {gid}")
        if not isinstance(guard_refs, list):
            raise CheckFailure(f"guard_refs-invalid: {gid}")
        if any(x in {"RETAINED", "CAMPAIGN"} for x in evidence_classes) and not evidence_refs:
            raise CheckFailure(f"retained-evidence-ref-missing: {gid}")
        if any(x in {"RETAINED", "CAMPAIGN"} for x in evidence_classes):
            retained_refs = [
                ref for ref in evidence_refs
                if isinstance(ref, dict)
                and isinstance(ref.get("path"), str)
                and (ref["path"].startswith("docs/evidence/") or "/docs/evidence/" in ref["path"])
                and ref["path"].endswith(".json")
            ]
            if not retained_refs:
                raise CheckFailure(f"retained-evidence-artifact-missing: {gid}")
        for ref in evidence_refs:
            checked = validate_ref(ref, roots, f"{gid}:evidence_refs")
            local_refs += int(checked)
            if not checked:
                external_skips.add(ref["repo"])
        for ref in guard_refs:
            checked = validate_guard_ref(ref, roots, f"{gid}:guard_refs")
            local_refs += int(checked)
            if not checked:
                external_skips.add(ref["repo"])
        if g.get("test_of_test") == "VERIFIED_GUARD":
            red_refs = g.get("test_of_test_evidence_refs")
            if not isinstance(red_refs, list) or not red_refs:
                raise CheckFailure(f"verified-guard-redrun-evidence-missing: {gid}")
            for ref in red_refs:
                if ref.get("repo") != "nift" or "docs/evidence" not in ref.get("path", "") or not ref.get("path", "").endswith(".json"):
                    raise CheckFailure(f"verified-guard-redrun-evidence-invalid: {gid}")
                checked = validate_ref(ref, roots, f"{gid}:test_of_test_evidence_refs")
                local_refs += int(checked)
                if not checked:
                    external_skips.add(ref["repo"])
        enforcement = g.get("enforcement")
        if not isinstance(enforcement, list) or not enforcement:
            raise CheckFailure(f"enforcement-missing: {gid}")
        for item in enforcement:
            if not isinstance(item, dict) or item.get("class") not in ALLOWED_ENFORCEMENT:
                raise CheckFailure(f"enforcement-invalid: {gid}: {item!r}")
            klass = item["class"]
            if klass == "MANUAL":
                if not isinstance(item.get("command"), str) or not item["command"].strip():
                    raise CheckFailure(f"manual-command-missing: {gid}")
                make_ref = make_target_from_command(item["command"])
                if make_ref is None:
                    raise CheckFailure(f"manual-command-unsupported: {gid}: {item['command']!r}")
                makefile_rel, target = make_ref
                makefile = nift_root / makefile_rel
                if not makefile_has_target(makefile, target):
                    raise CheckFailure(f"manual-make-target-missing: {gid}: {makefile_rel}:{target}")
                continue
            workflow = item.get("workflow")
            job = item.get("job")
            if not isinstance(workflow, str) or not isinstance(job, str):
                raise CheckFailure(f"workflow-ref-invalid: {gid}: {item!r}")
            wf_ref = {"repo": "nift", "path": workflow}
            wf_path, _ = path_for(wf_ref, roots)
            assert wf_path is not None
            if not wf_path.is_file():
                raise CheckFailure(f"workflow-missing: {gid}: {workflow}")
            if not workflow_has_job(wf_path, job):
                raise CheckFailure(f"workflow-job-missing: {gid}: {workflow}#{job}")
            validate_workflow_trigger(wf_path, klass, gid)
            ci_refs += 1

    audited_surfaces = 0
    for surface in claim_surfaces:
        if not isinstance(surface, dict):
            raise CheckFailure("public-claim-surface-invalid: expected object")
        expected_hash = surface.get("sha256")
        if not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            raise CheckFailure(f"public-claim-surface-hash-invalid: {expected_hash!r}")
        path, repo = path_for(surface, roots)
        if path is None:
            external_skips.add(repo)
            continue
        if not path.is_file():
            raise CheckFailure(f"public-claim-surface-missing: {repo}:{surface['path']}")
        observed = hashlib.sha256(path.read_bytes()).hexdigest()
        if observed != expected_hash:
            raise CheckFailure(f"public-claim-surface-stale: {repo}:{surface['path']}: expected {expected_hash}, observed {observed}")
        audited_surfaces += 1

    claim_ids: set[str] = set()
    for claim in claims:
        if not isinstance(claim, dict):
            raise CheckFailure("public-claim-invalid: expected object")
        cid = validate_id(claim.get("id"), "public-claim", claim_ids)
        if claim.get("state") != "ESTABLISHED":
            raise CheckFailure(f"public-claim-not-established: {cid}: {claim.get('state')!r}")
        gid = claim.get("guarantee_id")
        if gid not in guarantee_ids:
            raise CheckFailure(f"public-claim-guarantee-missing: {cid}: {gid!r}")
        guarantee = next(g for g in guarantees if g.get("id") == gid)
        if guarantee.get("state") != "ESTABLISHED":
            raise CheckFailure(f"public-claim-guarantee-not-established: {cid}: {gid}")
        source = claim.get("source")
        if not isinstance(source, dict) or (source.get("repo"), source.get("path")) not in pinned_surface_keys:
            raise CheckFailure(f"public-claim-source-not-pinned: {cid}")
        checked = validate_ref(source, roots, f"{cid}:source", require_needle=True)
        local_refs += int(checked)
        if not checked:
            external_skips.add(claim["source"]["repo"])

    discrepancy_ids: set[str] = set()
    for item in discrepancies:
        if not isinstance(item, dict):
            raise CheckFailure("known-discrepancy-invalid: expected object")
        did = validate_id(item.get("id"), "known-discrepancy", discrepancy_ids)
        if item.get("state") not in {"NOT_ESTABLISHED", "ACCEPTED_LIMITATION", "OUT_OF_SCOPE"}:
            raise CheckFailure(f"known-discrepancy-state-invalid: {did}: {item.get('state')!r}")
        refs = item.get("refs")
        if not isinstance(refs, list) or not refs:
            raise CheckFailure(f"known-discrepancy-refs-missing: {did}")
        for ref in refs:
            checked = validate_ref(ref, roots, f"{did}:ref", require_needle="needle" in ref)
            local_refs += int(checked)
            if not checked:
                external_skips.add(ref["repo"])

    meta_ids: set[str] = set()
    for item in meta_guards:
        if not isinstance(item, dict):
            raise CheckFailure("meta-guard-invalid: expected object")
        mid = validate_id(item.get("id"), "meta-guard", meta_ids)
        if item.get("test_of_test") not in ALLOWED_TOT:
            raise CheckFailure(f"meta-guard-test-of-test-invalid: {mid}")
        checked = validate_guard_ref(item.get("guard_ref"), roots, f"{mid}:guard-ref")
        local_refs += int(checked)
        if not checked:
            external_skips.add(item["guard_ref"]["repo"])

    return {
        "guarantees": len(guarantees),
        "public_claims": len(claims),
        "audited_public_claim_surfaces": audited_surfaces,
        "known_discrepancies": len(discrepancies),
        "meta_guards": len(meta_guards),
        "ci_job_refs": ci_refs,
        "checked_file_refs": local_refs,
        "unavailable_sibling_repositories": sorted(external_skips),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", default="docs/guarantees/registry.json")
    parser.add_argument("--nift-root")
    parser.add_argument("--website-root")
    parser.add_argument("--regression-root")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        summary = check(Path(args.registry), args)
    except CheckFailure as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    unavailable = summary["unavailable_sibling_repositories"]
    status = "SKIP" if unavailable else "PASS"
    if args.json:
        print(json.dumps({"status": status, **summary}, indent=2, sort_keys=True))
    elif unavailable:
        print(
            "SKIP: guarantee registry structural check incomplete; required sibling repositories unavailable: "
            + ", ".join(unavailable)
        )
        print(
            f"checked {summary['audited_public_claim_surfaces']} public claim surfaces; "
            "public-claim completeness is not asserted"
        )
    else:
        print(
            "PASS: guarantee registry structurally valid "
            f"({summary['guarantees']} guarantees, {summary['public_claims']} public claims, "
            f"{summary['known_discrepancies']} known discrepancies, {summary['ci_job_refs']} CI job refs)"
        )
    return 2 if unavailable else 0


if __name__ == "__main__":
    raise SystemExit(main())
