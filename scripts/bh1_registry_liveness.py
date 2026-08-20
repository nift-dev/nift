#!/usr/bin/env python3
"""BH1 implementer-side liveness precheck for the guarantee-registry checker.

This is not the independent reviewer sign-off. DeepSeek must author/run its own
withheld corruption after the checkpoint commit. This script proves the checker
is not trivially vacuous against a small set of representative structural faults.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


def run_checker(checker: Path, registry: Path, nift_root: Path, website: Path | None, regression: Path | None) -> subprocess.CompletedProcess[str]:
    cmd = [str(checker), "--registry", str(registry), "--nift-root", str(nift_root)]
    if website:
        cmd += ["--website-root", str(website)]
    if regression:
        cmd += ["--regression-root", str(regression)]
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--registry", default="docs/guarantees/registry.json")
    p.add_argument("--checker", default="scripts/check_guarantee_registry.py")
    p.add_argument("--website-root")
    p.add_argument("--regression-root")
    p.add_argument("--evidence", default=".build/bh1/registry-liveness.json")
    args = p.parse_args()

    registry = Path(args.registry).resolve()
    checker = Path(args.checker).resolve()
    website = Path(args.website_root).resolve() if args.website_root else None
    regression = Path(args.regression_root).resolve() if args.regression_root else None
    original = json.loads(registry.read_text(encoding="utf-8"))
    nift_root = registry.parents[2]

    cases = []

    def add_case(name: str, mutate, expected: str) -> None:
        data = copy.deepcopy(original)
        mutate(data)
        cases.append((name, data, expected))

    add_case(
        "missing-retained-evidence",
        lambda d: d["guarantees"][0]["evidence_refs"][0].update(path="docs/evidence/checkpoint-7/DOES-NOT-EXIST.json"),
        "evidence_refs-missing",
    )
    add_case(
        "nonexistent-ci-job",
        lambda d: d["guarantees"][4]["enforcement"][0].update(job="definitely-not-a-real-job"),
        "workflow-job-missing",
    )
    add_case(
        "duplicate-guarantee-id",
        lambda d: d["guarantees"][1].update(id=d["guarantees"][0]["id"]),
        "duplicate-id",
    )
    add_case(
        "broken-public-claim-needle",
        lambda d: d["public_claims"][0]["source"].update(needle="THIS CLAIM TEXT DOES NOT EXIST"),
        "needle-not-found",
    )
    add_case(
        "stale-public-claim-surface-hash",
        lambda d: d["public_claim_surfaces"][0].update(sha256="0" * 64),
        "public-claim-surface-stale",
    )

    results = []
    all_ok = True
    with tempfile.TemporaryDirectory(prefix="nift-bh1-red-") as td:
        root = Path(td)
        for name, data, expected in cases:
            path = root / f"{name}.json"
            path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
            proc = run_checker(checker, path, nift_root, website, regression)
            diagnostic = proc.stderr.strip() or proc.stdout.strip()
            passed = proc.returncode != 0 and expected in diagnostic
            all_ok &= passed
            results.append({
                "case": name,
                "expected_diagnostic_fragment": expected,
                "checker_exit": proc.returncode,
                "observed": diagnostic,
                "red_as_expected": passed,
            })

    def sha256(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    try:
        base_commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=nift_root, text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True).stdout.strip()
    except Exception:
        base_commit = None

    evidence = {
        "checkpoint": "BH1",
        "kind": "implementer-precheck",
        "candidate_parent_commit": base_commit,
        "registry_sha256": sha256(registry),
        "checker_sha256": sha256(checker),
        "status": "PASS" if all_ok else "FAIL",
        "note": "Independent reviewer liveness evidence is still required before BH1 closes.",
        "cases": results,
    }
    out = Path(args.evidence)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    for item in results:
        print(("PASS" if item["red_as_expected"] else "FAIL") + ": " + item["case"])
    print(f"evidence={out}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
