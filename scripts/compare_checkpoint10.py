#!/usr/bin/env python3
"""Compare Checkpoint 10 normalized evidence from all required runners."""
from __future__ import annotations

import argparse
import json
import pathlib


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("--output")
    return parser.parse_args()


def comparable(document):
    return {
        case["name"]: case["observation"]
        for case in document["cases"] if case["classification"] == "portable"
    }


def main():
    args = parse_args()
    documents = []
    for filename in args.inputs:
        path = pathlib.Path(filename)
        document = json.loads(path.read_text(encoding="utf-8"))
        if document.get("schema_version") != 1 or document.get("checkpoint") != "10-cross-platform-behavioural-equivalence":
            raise SystemExit(f"unsupported checkpoint evidence: {path}")
        if not document.get("pass") or not all(case.get("pass") for case in document["cases"]):
            raise SystemExit(f"platform evidence is not passing: {path}")
        documents.append((path, document))
    if len(documents) != 3:
        raise SystemExit(f"expected exactly 3 platform documents, received {len(documents)}")
    runner_names = [document["platform"]["runner_os"] for _, document in documents]
    if len(set(runner_names)) != 3:
        raise SystemExit(f"runner identities are not unique: {runner_names}")
    baseline_path, baseline_document = documents[0]
    baseline = comparable(baseline_document)
    mismatches = []
    for path, document in documents[1:]:
        candidate = comparable(document)
        if candidate != baseline:
            all_names = sorted(set(baseline) | set(candidate))
            for name in all_names:
                if baseline.get(name) != candidate.get(name):
                    mismatches.append({
                        "case": name,
                        "baseline": baseline_document["platform"]["runner_os"],
                        "candidate": document["platform"]["runner_os"],
                        "baseline_observation": baseline.get(name),
                        "candidate_observation": candidate.get(name),
                    })
    summary = {
        "schema_version": 1,
        "checkpoint": "10-cross-platform-comparison",
        "runners": sorted(runner_names),
        "portable_case_count": len(baseline),
        "platform_specific_contracts_passed": all(
            case["pass"] for _, document in documents for case in document["cases"]
            if case["classification"] == "platform-specific"),
        "mismatches": mismatches,
        "pass": not mismatches,
    }
    if args.output:
        output = pathlib.Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    if mismatches:
        print(json.dumps(summary, indent=2))
        raise SystemExit("checkpoint 10 portable observations differ")
    print(f"checkpoint 10 comparison: PASS ({len(baseline)} equivalent portable cases across {', '.join(sorted(runner_names))})")


if __name__ == "__main__":
    main()
